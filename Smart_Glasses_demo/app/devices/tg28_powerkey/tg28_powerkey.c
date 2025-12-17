


#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/spinlock.h>

/* ========================== 1. 硬件配置与宏定义 ========================== */
#define DRV_NAME             "tg28-powerkey"
#define DRV_VERSION          "v1.0"

/* TG28寄存器定义（硬件手册对齐） */
#define TG28_INTEN0         0x40   
#define TG28_INTEN1          0x41    // 中断使能: bit0=按下 bit1=抬起 bit2=长按 bit3=短按
#define TG28_INTEN2         0x42    


#define TG28_INTSTS0         0x48    // 中断状态（写1清0）: 与INTEN1位定义一致
#define TG28_INTSTS1         0x49    // 中断状态（写1清0）: 与INTEN1位定义一致
#define TG28_INTSTS2         0x4a    // 中断状态（写1清0）: 与INTEN1位定义一致
#define TG28_PONLEVEL        0x27    // 按键控制: bit5-4=长按时间配置

/* 中断状态位（写1清除） */
#define TG28_IRQ_PRESS       (1 << 0)  // 按键按下
#define TG28_IRQ_RELEASE     (1 << 1)  // 按键抬起
#define TG28_IRQ_SHORT       (1 << 3)  // 短按触发（硬件识别的完整短按）
#define TG28_IRQ_ALL         (TG28_IRQ_PRESS | TG28_IRQ_RELEASE | TG28_IRQ_SHORT)

/* 时间配置（避免魔法数） */
#define DEBOUNCE_MS          10      // 消抖时长(ms)
#define DEBOUNCE_JIFFIES     (msecs_to_jiffies(DEBOUNCE_MS))
#define DOUBLE_CLICK_MS      500     // 双击检测窗口(ms)
#define DOUBLE_CLICK_JIFFIES (msecs_to_jiffies(DOUBLE_CLICK_MS))
#define I2C_RETRY_CNT        3       // I2C重试次数
#define I2C_RETRY_DELAY_MS   5       // I2C重试延时(ms)

/* ========================== 2. 设备私有数据结构 ========================== */
struct tg28_dev {
    struct i2c_client      *client;
    struct input_dev       *input;
    struct work_struct     irq_work;          // 中断底半部
    struct delayed_work    double_click_dwork;// 双击检测延迟工作
    struct mutex           i2c_lock;          // I2C操作锁
    spinlock_t             irq_lock;          // 中断状态锁
    unsigned int           double_click_ms;   // 双击阈值(ms)
    unsigned long          last_irq_jiff;     // 上一次中断时间(消抖用)
    unsigned long          first_short_jiff;  // 第一次短按触发时间（核心：仅关联SHORT位）
    int                    gpio;              // PowerKey GPIO
    int                    irq;               // 中断号
    bool                   key_pressed;       // 按键当前状态
    bool                   irq_enabled;       // 中断使能标记
    bool                   irq_triggered;     // 中断触发标记
    bool                   is_first_short;    // 是否是第一次短按（仅关联SHORT位）
    u8                     inten1_backup;     // INTEN1寄存器备份
};

/* ========================== 3. 工具函数（I2C读写） ========================== */
static int tg28_i2c_read(struct tg28_dev *dev, u8 reg, u8 *val)
{
    s32 ret;
    int retry = I2C_RETRY_CNT;

    if (!dev || !val)
        return -EINVAL;

    mutex_lock(&dev->i2c_lock);
    while (retry--) {
        ret = i2c_smbus_read_byte_data(dev->client, reg);
        if (ret >= 0) {
            *val = (u8)ret;
            dev_dbg(&dev->client->dev, "[I2C READ] reg=0x%02x, val=0x%02x\n", reg, *val);
            mutex_unlock(&dev->i2c_lock);
            return 0;
        }
        msleep(I2C_RETRY_DELAY_MS);
        dev_warn(&dev->client->dev, "[I2C RETRY] reg=0x%02x, retry left=%d, err=%d\n", reg, retry, ret);
    }
    dev_err(&dev->client->dev, "[I2C READ FAIL] reg=0x%02x, err=%d\n", reg, ret);
    mutex_unlock(&dev->i2c_lock);
    return ret;
}

static int tg28_i2c_write(struct tg28_dev *dev, u8 reg, u8 val)
{
    s32 ret;
    int retry = I2C_RETRY_CNT;

    if (!dev)
        return -EINVAL;

    mutex_lock(&dev->i2c_lock);
    while (retry--) {
        ret = i2c_smbus_write_byte_data(dev->client, reg, val);
        if (ret >= 0) {
            dev_dbg(&dev->client->dev, "[I2C WRITE] reg=0x%02x, val=0x%02x\n", reg, val);
            mutex_unlock(&dev->i2c_lock);
            return 0;
        }
        msleep(I2C_RETRY_DELAY_MS);
        dev_warn(&dev->client->dev, "[I2C WRITE RETRY] reg=0x%02x, retry left=%d, err=%d\n", reg, retry, ret);
    }



    dev_err(&dev->client->dev, "[I2C FAIL] reg=0x%02x, err=%d\n", reg, ret);
    mutex_unlock(&dev->i2c_lock);

        return ret;
}

/* ========================== 4. 中断相关核心函数 ========================== */
static int tg28_clear_irq_status(struct tg28_dev *dev)
{
    int ret = tg28_i2c_write(dev, TG28_INTSTS1, TG28_IRQ_ALL);
    if (ret >= 0) {
        dev_dbg(&dev->client->dev, "[IRQ] Clear INTSTS0 (write 0x%02x)\n", TG28_IRQ_ALL);
    
    }
    return ret;
}

static int tg28_enable_irq(struct tg28_dev *dev)
{
    unsigned long flags;
    int ret;
    ret = tg28_i2c_write(dev, TG28_INTEN1, TG28_IRQ_ALL);
    if (ret < 0) {
        return ret;
    }
    spin_lock_irqsave(&dev->irq_lock, flags);
    dev->irq_enabled = true;
    spin_unlock_irqrestore(&dev->irq_lock, flags);
    dev_dbg(&dev->client->dev, "[IRQ] Enabled (INTEN1=0x%02x)\n", TG28_IRQ_ALL);
    return 0;
}

static int tg28_disable_irq(struct tg28_dev *dev)
{
    unsigned long flags;
    int ret;
    ret = tg28_i2c_write(dev, TG28_INTEN0, 0x00);
    if (ret < 0) {
        return ret;
    }
    ret = tg28_i2c_write(dev, TG28_INTEN1, 0x00);
    if (ret < 0) {
        return ret;
    }
    ret = tg28_i2c_write(dev, TG28_INTEN2, 0x00);
    if (ret < 0) {
        return ret;
    }
    spin_lock_irqsave(&dev->irq_lock, flags);
    dev->irq_enabled = false;
    spin_unlock_irqrestore(&dev->irq_lock, flags);
    dev_dbg(&dev->client->dev, "[IRQ] Disabled\n");
    return ret;
}

static irqreturn_t tg28_irq_handler(int irq, void *dev_id)
{
    struct tg28_dev *dev = dev_id;
    unsigned long flags;

    // 检查中断使能状态
    spin_lock_irqsave(&dev->irq_lock, flags);
    if (!dev->irq_enabled) {
        spin_unlock_irqrestore(&dev->irq_lock, flags);
        dev_dbg(&dev->client->dev, "[IRQ] Disabled, skip\n");
        return IRQ_NONE;
    }
    dev->irq_triggered = true;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    // 调度底半部处理

    schedule_work(&dev->irq_work);
    dev_dbg(&dev->client->dev, "[IRQ] Triggered, schedule work\n");

    return IRQ_HANDLED;
}

/**
 * tg28_single_short_work - 延迟工作：双击窗口超时，上报单次短按
 * @work: 延迟工作结构体
 */
static void tg28_single_short_work(struct work_struct *work)
{
   
    struct tg28_dev *dev = container_of(work, struct tg28_dev, double_click_dwork.work);
    unsigned long flags;

    // 自旋锁保护状态标记（仅处理第一次短按超时的情况）
    spin_lock_irqsave(&dev->irq_lock, flags);
    if (dev->is_first_short) {
        // 上报单次短按（KEY_POWER）
        input_report_key(dev->input, KEY_POWER, 1);
        input_sync(dev->input);
        input_report_key(dev->input, KEY_POWER, 0);
        input_sync(dev->input);
        dev_info(&dev->client->dev, "[KEY] Single short press (double click timeout)\n");
        
        // 重置短按状态（仅关联SHORT位）
        dev->is_first_short = false;
        dev->first_short_jiff = 0;
    }
    spin_unlock_irqrestore(&dev->irq_lock, flags);
}

/**
 * tg28_irq_work_handler - 中断底半部（核心：仅基于SHORT位判断双击/短按）
 * @work: 工作队列结构体
 */
static void tg28_irq_work_handler(struct work_struct *work)
{
    struct tg28_dev *dev = container_of(work, struct tg28_dev, irq_work);
    unsigned long now = jiffies;
    u8 int_status = 0;
    unsigned long flags;
    int ret;
    // 消抖检查：10ms内重复中断忽略
    if (time_before(now, dev->last_irq_jiff + DEBOUNCE_JIFFIES)) {
        dev_dbg(&dev->client->dev, "[IRQ] Debounce (%dms), skip\n", DEBOUNCE_MS);
        recl:
        ret = tg28_clear_irq_status(dev);
        if (ret < 0) {
            msleep(5);
            goto recl;
        }
        dev->last_irq_jiff = now;
        return;
    }
    dev->last_irq_jiff = now;
    // 检查中断触发标记
    spin_lock_irqsave(&dev->irq_lock, flags);
    if (!dev->irq_triggered || !dev->irq_enabled) {
        spin_unlock_irqrestore(&dev->irq_lock, flags);
        dev_dbg(&dev->client->dev, "[WORK] No valid trigger, skip\n");
        return;
    }
    dev->irq_triggered = false;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    // 读取中断状态（重试确保成功）
    msleep(30);

    reread_sts1:
    if (tg28_i2c_read(dev, TG28_INTSTS1, &int_status)) {
        msleep(5);
        goto reread_sts1;
    }
    dev_dbg(&dev->client->dev, "[WORK] INTSTS1=0x%02x (PRESS:%d RELEASE:%d SHORT:%d)\n",
            int_status, !!(int_status & TG28_IRQ_PRESS), !!(int_status & TG28_IRQ_RELEASE),
            !!(int_status & TG28_IRQ_SHORT));

    // 无有效中断状态，直接返回
    if (!(int_status & TG28_IRQ_ALL)) {
        dev_dbg(&dev->client->dev, "[WORK] No valid key event\n");
        goto out_restore_irq;
    }

    // 仅记录按键按下状态（不上报、不参与双击判断）
    if (int_status & TG28_IRQ_PRESS) {
        dev->key_pressed = true;
        dev_info(&dev->client->dev, "[KEY] Pressed (only record status)\n");
    }

    // 仅重置按键抬起状态（不上报、不参与双击判断）
    if (int_status & TG28_IRQ_RELEASE) {
        dev->key_pressed = false;
        dev_info(&dev->client->dev, "[KEY] Released (only reset status)\n");
    }

    // ========== 核心逻辑：仅基于硬件SHORT位判断双击/短按 ==========
    if (int_status & TG28_IRQ_SHORT) {
        spin_lock_irqsave(&dev->irq_lock, flags);
        
        if (!dev->is_first_short) {
            // 第一次短按：启动双击检测延迟工作（300ms窗口）
            dev->is_first_short = true;
            dev->first_short_jiff = now;
            queue_delayed_work(system_wq, &dev->double_click_dwork, DOUBLE_CLICK_JIFFIES);
            dev_info(&dev->client->dev, "[KEY] First hardware short press, start double click timer (%dms)\n", 
                    dev->double_click_ms);
        } else {
            // 第二次短按：判断是否在双击窗口内
            if (time_before(now, dev->first_short_jiff + DOUBLE_CLICK_JIFFIES)) {
                // 双击：取消延迟工作，仅上报双击（KEY_RESTART），不上报两次短按
                cancel_delayed_work_sync(&dev->double_click_dwork);
                
                input_report_key(dev->input, KEY_RESTART, 1);
                input_sync(dev->input);
                input_report_key(dev->input, KEY_RESTART, 0);
                input_sync(dev->input);
                dev_info(&dev->client->dev, "[KEY] Double short press detected (interval:%dms), report RESTART\n",
                        jiffies_to_msecs(now - dev->first_short_jiff));
                
                // 重置短按状态
                dev->is_first_short = false;
                dev->first_short_jiff = 0;
            } else {
                // 第二次短按超出窗口：视为两次独立短按
                cancel_delayed_work_sync(&dev->double_click_dwork);
                
                // 先上报第一次短按（超时的）
                input_report_key(dev->input, KEY_POWER, 1);
                input_sync(dev->input);
                input_report_key(dev->input, KEY_POWER, 0);
                input_sync(dev->input);
                dev_info(&dev->client->dev, "[KEY] First short press (timeout), report single press\n");
                
                // 标记第二次短按为新的第一次
                dev->first_short_jiff = now;
                queue_delayed_work(system_wq, &dev->double_click_dwork, DOUBLE_CLICK_JIFFIES);
                dev_info(&dev->client->dev, "[KEY] Second short press (timeout), start new double click timer\n");
            }
        }
        spin_unlock_irqrestore(&dev->irq_lock, flags);
    }
out_restore_irq:
    // 清除中断状态 + 重新使能中断（避免IRQ引脚卡滞）
    tg28_clear_irq_status(dev);
    tg28_enable_irq(dev);
}

/* ========================== 5. 硬件初始化与设备树解析 ========================== */
static int tg28_parse_dt(struct device *dev, struct tg28_dev *tg28)
{
    struct device_node *np = dev->of_node;

    // 默认参数
    tg28->double_click_ms = DOUBLE_CLICK_MS;

    // 解析可选参数（双击阈值）
    of_property_read_u32(np, "tg28,double-click-ms", &tg28->double_click_ms);

    // 解析GPIO
    tg28->gpio = of_get_named_gpio_flags(np, "powerkey-gpio", 0, NULL);
    if (!gpio_is_valid(tg28->gpio)) {
        dev_err(dev, "Invalid powerkey GPIO: %d\n", tg28->gpio);
        return -EINVAL;
    }

    // 解析中断号
    tg28->irq = irq_of_parse_and_map(np, 0);
    if (tg28->irq <= 0 || tg28->irq == NO_IRQ) {
        dev_err(dev, "Invalid IRQ number: %d\n", tg28->irq);
        return -EINVAL;
    }

    dev_info(dev, "[DT] GPIO:%d IRQ:%d DoubleClick:%dms (based on SHORT IRQ)\n",
             tg28->gpio, tg28->irq, tg28->double_click_ms);
    return 0;
}

static int tg28_hw_init(struct tg28_dev *tg28)
{
    int ret;
    u8 pon_level = 0;
    int gpio_value=-1;
    // 初始化GPIO
    ret = devm_gpio_request(&tg28->client->dev, tg28->gpio, DRV_NAME "-gpio");
    if (ret) {
        dev_err(&tg28->client->dev, "GPIO request failed: %d\n", ret);
        return ret;
    }
    gpio_direction_input(tg28->gpio);
   
    // 备份INTEN1寄存器
    ret = tg28_i2c_read(tg28, TG28_INTEN1, &tg28->inten1_backup);
    if (ret < 0) {
        return ret;
    }
    dev_info(&tg28->client->dev, "[INIT] Backup INTEN1=0x%02x\n", tg28->inten1_backup);
    // 配置PONLEVEL（清空长按位，仅保留短按/双击相关）
    ret = tg28_i2c_read(tg28, TG28_PONLEVEL, &pon_level);
    if (ret < 0) {
        return ret;
    }
    pon_level &= 0xCF; // 清空bit5-4（长按配置位）
    ret = tg28_i2c_write(tg28, TG28_PONLEVEL, pon_level);
    if (ret < 0) {
        return ret;
    }
    dev_info(&tg28->client->dev, "[INIT] PONLEVEL=0x%02x (only short/double click)\n", pon_level);

    // 初始化中断状态
    tg28_disable_irq(tg28);
    if(ret<0)
    {
        return ret;
    }
    ret = tg28_clear_irq_status(tg28);
    if (ret < 0) {
        return ret;
    }
    ret = tg28_enable_irq(tg28);
    if (ret < 0) {
        return ret;
    }

    gpio_value=gpio_get_value(tg28->gpio);
    printk("after gpio = %d\n",gpio_value);


    // 申请中断（下降沿+ONESHOT，避免嵌套）
    ret = devm_request_threaded_irq(&tg28->client->dev, tg28->irq,
                                   NULL, tg28_irq_handler,
                                   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                   DRV_NAME, tg28);
    if (ret) {
        dev_err(&tg28->client->dev, "IRQ request failed: %d\n", ret);
        tg28_disable_irq(tg28);
        return ret;
    }

    // 初始化短按/双击状态（仅关联SHORT位）
    tg28->is_first_short = false;
    tg28->first_short_jiff = 0;
    tg28->key_pressed = false;
    tg28->last_irq_jiff = 0;

    return 0;
}





/**
 * pmic_i2c_reg_config - 配置指定I2C设备的电源管理寄存器
 * @client: I2C设备客户端指针（标识BUS=3、ADDR=0x34的I2C设备）
 * return: 0成功，负数内核错误码（如-EIO表示I2C通信失败）
 */
static int pmic_i2c_reg_config(struct tg28_dev *tg28)
{
    int ret;
    u8 reg_val;  // 存储寄存器读取值
    struct i2c_client      *client=tg28->client;
 

    // 1) 配置VINDPM 输入电压限制4.36V：0x15寄存器写入0x06
    ret = i2c_smbus_write_byte_data(client, 0x15, 0x06);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x15 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x15 write 0x06 success\n");

    // 2) 输入限流500mA：0x16寄存器写入0x03（保守值）
    ret = i2c_smbus_write_byte_data(client, 0x16, 0x03);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x16 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x16 write 0x03 success (500mA limit)\n");

    // 3) 预充/快充配置：0x61写0x04 预充电电流限制100mA  0x62写0x0B  直流充电电流限制500mA
    ret = i2c_smbus_write_byte_data(client, 0x61, 0x04);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x61 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x61 write 0x04 success\n");

    ret = i2c_smbus_write_byte_data(client, 0x62, 0x0B);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x62 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x62 write 0x0B success\n");

    // 4) 终止电流：读改写0x63（保留高4位，低4位设0x04）
    reg_val = i2c_smbus_read_byte_data(client, 0x63);
   
    if (reg_val < 0) {  // 读失败返回负数（内核接口特性）
        dev_err(&client->dev, "Read reg 0x63 failed, ret=%d\n", reg_val);
        return reg_val;
    }
    // 位操作：保留高4位（0xF0），低4位设为0x04
    reg_val = (reg_val & 0xF0) | 0x04;
    ret = i2c_smbus_write_byte_data(client, 0x63, reg_val);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x63 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x63 read-modify-write success (new val=0x%02X)\n", reg_val);

    // 5) ADC全开：0x30寄存器写入0x1F
    ret = i2c_smbus_write_byte_data(client, 0x30, 0x1F);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x30 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x30 write 0x1F success (ADC all on)\n");

    // 额外配置：0x14寄存器写入0x05
    ret = i2c_smbus_write_byte_data(client, 0x14, 0x05);
    if (ret < 0) {
        dev_err(&client->dev, "Write reg 0x14 failed, ret=%d\n", ret);
        return ret;
    }
    dev_dbg(&client->dev, "Reg 0x14 write 0x05 success\n");

    dev_info(&client->dev, "All PMIC reg config success!\n");
    return 0;
}

/* ========================== 6. 驱动框架（Probe/Remove） ========================== */
static int tg28_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct tg28_dev *tg28;
    struct input_dev *input;
    int ret;

    // 检查I2C功能
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "I2C adapter not support SMBUS_BYTE\n");
        return -EOPNOTSUPP;
    }

    // 分配私有数据
    tg28 = devm_kzalloc(&client->dev, sizeof(*tg28), GFP_KERNEL);
    if (!tg28)
        return -ENOMEM;
    tg28->client = client;
    i2c_set_clientdata(client, tg28);

    // 初始化锁和工作队列
    mutex_init(&tg28->i2c_lock);
    spin_lock_init(&tg28->irq_lock);
    INIT_WORK(&tg28->irq_work, tg28_irq_work_handler);
    // 初始化双击检测延迟工作（关联SHORT位）
    INIT_DELAYED_WORK(&tg28->double_click_dwork, tg28_single_short_work);

    // 解析设备树
    ret = tg28_parse_dt(&client->dev, tg28);
    if (ret)
        return ret;

    // 初始化输入子系统
    input = devm_input_allocate_device(&client->dev);
    if (!input)
        return -ENOMEM;

    tg28->input = input;
    input->name = DRV_NAME;
    input->phys = DRV_NAME "/input0";
    input->id.bustype = BUS_I2C;

    // 注册按键事件（仅保留短按KEY_POWER + 双击KEY_RESTART）
    __set_bit(EV_KEY, input->evbit);
    __set_bit(KEY_POWER, input->keybit);    // 单次短按
    __set_bit(KEY_RESTART, input->keybit);  // 双击

    ret = input_register_device(input);
    if (ret) {
        dev_err(&client->dev, "Input register failed: %d\n", ret);
        return ret;
    }

    // 硬件初始化
    ret = tg28_hw_init(tg28);
    if (ret) {
        input_unregister_device(input);
        return ret;
    }
    ret = pmic_i2c_reg_config(tg28);
    if(ret<0)
    {
        input_unregister_device(input);
        return ret;
    }

    dev_info(&client->dev, "[PROBE] %s v%s init success (double click based on SHORT IRQ)\n", DRV_NAME, DRV_VERSION);
    return 0;
}

static int tg28_remove(struct i2c_client *client)
{
    struct tg28_dev *tg28 = i2c_get_clientdata(client);

    // 禁用中断
    tg28_disable_irq(tg28);

    // 停止所有工作队列（包括延迟工作）
    cancel_work_sync(&tg28->irq_work);
    cancel_delayed_work_sync(&tg28->double_click_dwork);

    // 恢复INTEN1寄存器
    if (tg28->inten1_backup != 0) {
        tg28_i2c_write(tg28, TG28_INTEN1, tg28->inten1_backup);
        dev_info(&client->dev, "[REMOVE] Restore INTEN1=0x%02x\n", tg28->inten1_backup);
    }

    // 注销输入设备
    input_unregister_device(tg28->input);

    dev_info(&client->dev, "[REMOVE] %s driver removed\n", DRV_NAME);
    return 0;
}

/* ========================== 7. 驱动注册 ========================== */
static const struct i2c_device_id tg28_id_table[] = {
    { DRV_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, tg28_id_table);

static const struct of_device_id tg28_of_match[] = {
    { .compatible = "tg28,powerkey" },
    { }
};
MODULE_DEVICE_TABLE(of, tg28_of_match);

static struct i2c_driver tg28_driver = {
    .driver = {
        .name = DRV_NAME,
        .of_match_table = tg28_of_match,
    },
    .probe = tg28_probe,
    .remove = tg28_remove,
    .id_table = tg28_id_table,
};

module_i2c_driver(tg28_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TG28 Power Key Driver (Double Click Based on SHORT IRQ, No RELEASE Confusion)");
MODULE_AUTHOR("YUANWANG");
MODULE_VERSION(DRV_VERSION);
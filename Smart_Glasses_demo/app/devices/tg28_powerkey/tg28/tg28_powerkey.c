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

/* ========================== 1. 驱动基础配置 ========================== */
#define DRV_NAME        "tg28-powerkey"  // 驱动名称
#define DRV_VERSION     "v1.0"            // 驱动版本

/* ========================== 2. TG28寄存器定义（硬件手册对齐） ========================== */
#define TG28_REG_INTEN0     0x40    // 中断使能寄存器0
#define TG28_REG_INTEN1     0x41    // 中断使能寄存器1（核心）
                                    // bit0: 按键按下中断使能
                                    // bit1: 按键松开中断使能
                                    // bit2: 长按事件中断使能
                                    // bit3: 短按事件中断使能
#define TG28_REG_INTEN2     0x42    // 中断使能寄存器2

#define TG28_REG_INTSTS0    0x48    // 中断状态寄存器0（写1清0）
#define TG28_REG_INTSTS1    0x49    // 中断状态寄存器1（写1清0，核心）
#define TG28_REG_INTSTS2    0x4a    // 中断状态寄存器2（写1清0）
#define TG28_REG_PONLEVEL   0x27    // 按键控制寄存器：bit5-4配置长按时间

/* ========================== 3. 中断状态位定义（写1清除） ========================== */
#define TG28_IRQ_PRESS      (1 << 0)  // 按键按下事件
#define TG28_IRQ_RELEASE    (1 << 1)  // 按键松开事件
#define TG28_IRQ_LONG       (1 << 2)  // 长按触发事件（硬件识别）
#define TG28_IRQ_SHORT      (1 << 3)  // 短按触发事件（硬件识别）
#define TG28_IRQ_ALL        (TG28_IRQ_PRESS | TG28_IRQ_RELEASE | TG28_IRQ_LONG | TG28_IRQ_SHORT)
//#define TG28_IRQ_ALL        (TG28_IRQ_PRESS)

//#define TG28_IRQ_ALL        (TG28_IRQ_PRESS  | TG28_IRQ_LONG | TG28_IRQ_SHORT)

/* ========================== 4. 时间/重试配置（避免魔法数） ========================== */
#define DEBOUNCE_MS         5      // 消抖时长：50ms适配机械按键物理特性
#define DEBOUNCE_JIFFIES    (msecs_to_jiffies(DEBOUNCE_MS))
#define DOUBLE_CLICK_MS     500     // 双击检测窗口：500ms符合用户操作习惯
#define DOUBLE_CLICK_JIFFIES (msecs_to_jiffies(DOUBLE_CLICK_MS))
#define I2C_RETRY_CNT       3       // I2C通信重试次数
#define I2C_RETRY_DELAY_MS  5       // I2C重试延时：5ms避免频繁重试
#define INTSTS_RETRY_MAX    3       // 中断状态寄存器读取最大重试次数（防死循环）

/* ========================== 5. 设备私有数据结构 ========================== */
struct tg28_dev {
    struct i2c_client      *client;        // I2C设备客户端
    struct input_dev       *input;         // 输入子系统设备
    struct work_struct     irq_work;       // 中断底半部工作队列
    struct delayed_work    double_click_dwork;  // 双击检测延迟工作
    struct mutex           i2c_lock;       // I2C操作互斥锁（保护I2C读写）
    spinlock_t             irq_lock;       // 中断状态自旋锁（保护临界区）
    unsigned int           double_click_ms;// 双击检测阈值（ms）
    unsigned long          last_irq_jiff;  // 上一次中断触发时间（消抖用）
    unsigned long          first_short_jiff; // 第一次短按触发时间（双击判断）
    int                    gpio;           // PowerKey对应的GPIO号
    int                    irq;            // 中断号
    bool                   key_pressed;    // 按键当前状态：true-按下，false-松开
    bool                   irq_enabled;    // 中断使能标记
    bool                   irq_triggered;  // 中断触发标记
    bool                   is_first_short; // 双击检测：是否是第一次短按
    int                    led_flags;      // LED状态：0-灭，1-亮
    u8                     inten1_backup;  // INTEN1寄存器初始值备份（卸载恢复）
};

/* ========================== 6. 外部函数声明（WS2812灯控） ========================== */
//extern int ws2812_red_open(void);    // 打开红色LED
//extern int ws2812_red_close(void);   // 关闭红色LED
extern int ws2812_blink_red(void);   // 红色LED闪烁（备用）

/* ========================== 7. I2C通信工具函数 ========================== */
/**
 * tg28_i2c_read - 安全读取TG28寄存器值（带重试）
 * @dev: 设备私有数据
 * @reg: 寄存器地址
 * @val: 输出参数，存储读取到的寄存器值
 * 返回：0成功，负数内核错误码
 */
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
            dev_dbg(&dev->client->dev, "[TG28 I2C] 读寄存器 0x%02x = 0x%02x\n", reg, *val);
            mutex_unlock(&dev->i2c_lock);
            return 0;
        }
        msleep(I2C_RETRY_DELAY_MS);
        dev_warn(&dev->client->dev, "[TG28 I2C] 读寄存器 0x%02x 失败，剩余重试次数：%d，错误码：%d\n",
                 reg, retry, ret);
    }
    dev_err(&dev->client->dev, "[TG28 I2C] 读寄存器 0x%02x 最终失败，错误码：%d\n", reg, ret);
    mutex_unlock(&dev->i2c_lock);
    return ret;
}

/**
 * tg28_i2c_write - 安全写入TG28寄存器值（带重试）
 * @dev: 设备私有数据
 * @reg: 寄存器地址
 * @val: 要写入的值
 * 返回：0成功，负数内核错误码
 */
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
            dev_dbg(&dev->client->dev, "[TG28 I2C] 写寄存器 0x%02x = 0x%02x\n", reg, val);
            mutex_unlock(&dev->i2c_lock);
            return 0;
        }
        msleep(I2C_RETRY_DELAY_MS);
        dev_warn(&dev->client->dev, "[TG28 I2C] 写寄存器 0x%02x 失败，剩余重试次数：%d，错误码：%d\n",
                 reg, retry, ret);
    }
    dev_err(&dev->client->dev, "[TG28 I2C] 写寄存器 0x%02x 最终失败，错误码：%d\n", reg, ret);
    mutex_unlock(&dev->i2c_lock);
    return ret;
}

/* ========================== 8. 中断状态管理函数 ========================== */
/**
 * tg28_clear_irq_status - 清除所有中断状态位
 * @dev: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int tg28_clear_irq_status(struct tg28_dev *dev)
{
    int ret = tg28_i2c_write(dev, TG28_REG_INTSTS1, TG28_IRQ_ALL);
    if (ret >= 0) {
        dev_dbg(&dev->client->dev, "[TG28 IRQ] 清除所有中断状态（写入0x%02x）\n", TG28_IRQ_ALL);
    }
    return ret;
}

/**
 * tg28_clear_irq_bit - 精确清除指定中断状态位（读-改-写，不影响其他位）
 * @dev: 设备私有数据
 * @bit: 要清除的中断位（如TG28_IRQ_RELEASE）
 * 返回：0成功，负数内核错误码
 */
static int tg28_clear_irq_bit(struct tg28_dev *dev, u8 bit)
{
    u8 curr_status, new_status;
    int ret;

    if (!dev || bit == 0) {
        dev_dbg(&dev->client->dev, "[TG28 IRQ] 清除中断位参数无效：bit=0x%02x\n", bit);
        return -EINVAL;
    }

    // 1. 读取当前寄存器状态（保留其他位）
    ret = tg28_i2c_read(dev, TG28_REG_INTSTS1, &curr_status);
    if (ret < 0) {
        dev_err(&dev->client->dev, "[TG28 IRQ] 清除位0x%02x前读寄存器失败，错误码：%d\n", bit, ret);
        return ret;
    }

    // 2. 仅置位目标位（硬件特性：写1清0），其他位保持不变
    new_status = curr_status | bit;

    // 3. 写回寄存器（仅清除目标位）
    ret = tg28_i2c_write(dev, TG28_REG_INTSTS1, new_status);
    if (ret >= 0) {
        dev_dbg(&dev->client->dev, "[TG28 IRQ] 清除位0x%02x：当前值0x%02x → 写入值0x%02x\n",
                bit, curr_status, new_status);
    } else {
        dev_err(&dev->client->dev, "[TG28 IRQ] 清除位0x%02x失败，错误码：%d\n", bit, ret);
    }
    return ret;
}

/**
 * tg28_enable_irq - 使能TG28所有中断
 * @dev: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int tg28_enable_irq(struct tg28_dev *dev)
{
    unsigned long flags;
    int ret;

    ret = tg28_i2c_write(dev, TG28_REG_INTEN1, TG28_IRQ_ALL);
    if (ret < 0) {
        return ret;
    }

    spin_lock_irqsave(&dev->irq_lock, flags);
    dev->irq_enabled = true;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    dev_dbg(&dev->client->dev, "[TG28 IRQ] 使能所有中断（INTEN1=0x%02x）\n", TG28_IRQ_ALL);
    return 0;
}

/**
 * tg28_disable_irq_partial - 仅禁用INTEN1寄存器中断（轻量级禁用）
 * @dev: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int tg28_disable_irq_partial(struct tg28_dev *dev)
{
    unsigned long flags;
    int ret;

    ret = tg28_i2c_write(dev, TG28_REG_INTEN1, 0x00);
    if (ret < 0) {
        return ret;
    }

    spin_lock_irqsave(&dev->irq_lock, flags);
    dev->irq_enabled = false;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    dev_dbg(&dev->client->dev, "[TG28 IRQ] 禁用INTEN1中断\n");
    return ret;
}

/**
 * tg28_disable_irq_full - 完全禁用所有中断（INTEN0/1/2）
 * @dev: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int tg28_disable_irq_full(struct tg28_dev *dev)
{
    unsigned long flags;
    int ret;

    ret = tg28_i2c_write(dev, TG28_REG_INTEN0, 0x00);
    if (ret < 0) return ret;

    ret = tg28_i2c_write(dev, TG28_REG_INTEN1, 0x00);
    if (ret < 0) return ret;

    ret = tg28_i2c_write(dev, TG28_REG_INTEN2, 0x00);
    if (ret < 0) return ret;

    spin_lock_irqsave(&dev->irq_lock, flags);
    dev->irq_enabled = false;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    dev_dbg(&dev->client->dev, "[TG28 IRQ] 完全禁用所有中断\n");
    return ret;
}

/* ========================== 9. 中断处理核心函数 ========================== */
/**
 * tg28_irq_handler - 中断顶半部（仅标记触发，调度底半部）
 * @irq: 中断号
 * @dev_id: 设备私有数据指针
 * 返回：IRQ_HANDLED-处理成功，IRQ_NONE-未处理
 */
static irqreturn_t tg28_irq_handler(int irq, void *dev_id)
{
    struct tg28_dev *dev = dev_id;
    unsigned long flags;

    // 检查中断使能状态
    spin_lock_irqsave(&dev->irq_lock, flags);
    if (!dev->irq_enabled) {
        spin_unlock_irqrestore(&dev->irq_lock, flags);
        dev_dbg(&dev->client->dev, "[TG28 IRQ] 中断已禁用，跳过处理\n");
        return IRQ_NONE;
    }
    dev->irq_triggered = true;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    // 调度底半部处理（避免中断上下文耗时操作）
    schedule_work(&dev->irq_work);
    dev_dbg(&dev->client->dev, "[TG28 IRQ] 触发中断，调度底半部处理\n");

    return IRQ_HANDLED;
}

/**
 * tg28_single_short_work - 双击窗口超时处理：上报单次短按
 * @work: 延迟工作结构体指针
 */
static void tg28_single_short_work(struct work_struct *work)
{
    struct tg28_dev *dev = container_of(work, struct tg28_dev, double_click_dwork.work);
    unsigned long flags;

    spin_lock_irqsave(&dev->irq_lock, flags);
    if (dev->is_first_short) {
        // 上报单次短按事件（KEY_POWER）
        input_report_key(dev->input, KEY_POWER, 1);
        input_sync(dev->input);
        input_report_key(dev->input, KEY_POWER, 0);
        input_sync(dev->input);

        dev_info(&dev->client->dev, "[TG28 KEY] 双击窗口超时，上报单次短按\n");

        // 重置双击状态
        dev->is_first_short = false;
        dev->first_short_jiff = 0;
    }
    spin_unlock_irqrestore(&dev->irq_lock, flags);
}

/**
 * tg28_irq_work_handler - 中断底半部（处理按键核心逻辑）
 * @work: 工作结构体指针
 */
static void tg28_irq_work_handler(struct work_struct *work)
{
    struct tg28_dev *dev = container_of(work, struct tg28_dev, irq_work);
    unsigned long now = jiffies;
    u8 int_status = 0;
    unsigned long flags;
    int ret, retry_cnt = 0;
    bool clear_flag = false;

    dev_dbg(&dev->client->dev, "[TG28 WORK] 开始处理中断底半部\n");

    // 1. 消抖检查：5ms内重复中断忽略
    if (time_before(now, dev->last_irq_jiff + DEBOUNCE_JIFFIES)) {
        dev_dbg(&dev->client->dev, "[TG28 WORK] 消抖过滤（%dms内重复中断）\n", DEBOUNCE_MS);
        goto out_restore_irq;
    }
    dev->last_irq_jiff = now;

    // 2. 检查中断触发标记
    spin_lock_irqsave(&dev->irq_lock, flags);
    if (!dev->irq_triggered || !dev->irq_enabled) {
        spin_unlock_irqrestore(&dev->irq_lock, flags);
        dev_dbg(&dev->client->dev, "[TG28 WORK] 无有效中断触发，跳过\n");
        return;
    }
    dev->irq_triggered = false;
    spin_unlock_irqrestore(&dev->irq_lock, flags);

    // 3. 短延时等待硬件状态稳定
    msleep(10);

    // 4. 读取中断状态（带最大重试限制，防死循环）
retry_read_intsts:
    ret = tg28_i2c_read(dev, TG28_REG_INTSTS1, &int_status);
    if (ret < 0) {
        dev_err(&dev->client->dev, "[TG28 WORK] 读取中断状态失败，错误码：%d\n", ret);
        if (++retry_cnt < INTSTS_RETRY_MAX) {
            msleep(I2C_RETRY_DELAY_MS);
            goto retry_read_intsts;
        } else {
            dev_err(&dev->client->dev, "[TG28 WORK] 读取中断状态超过最大重试次数（%d次）\n", INTSTS_RETRY_MAX);
            goto out_restore_irq;
        }
    }

   // 5. 禁用中断（避免处理过程中重复触发）
    ret = tg28_disable_irq_partial(dev);
    if (ret < 0) {
        dev_err(&dev->client->dev, "[TG28 WORK] 禁用中断失败，错误码：%d\n", ret);
        goto out_restore_irq;
    }

    // 打印中断状态（调试用）
    printk("[TG28 WORK] 中断状态0x%02x → 按下:%d 松开:%d 长按:%d 短按:%d\n",
            int_status, !!(int_status & TG28_IRQ_PRESS), !!(int_status & TG28_IRQ_RELEASE),
            !!(int_status & TG28_IRQ_LONG), !!(int_status & TG28_IRQ_SHORT));

    // 6. 无有效中断状态，直接恢复
    if (!(int_status & TG28_IRQ_ALL)) {
        dev_dbg(&dev->client->dev, "[TG28 WORK] 无有效按键事件\n");
        goto out_restore_irq;
    }

    // 7. 处理按键按下事件（仅记录状态，不上报）
    if (int_status & TG28_IRQ_PRESS) {
        dev->key_pressed = true;
        tg28_clear_irq_bit(dev, TG28_IRQ_PRESS);
        clear_flag = true;
        dev_info(&dev->client->dev, "[TG28 KEY] 按键按下（记录状态）\n");
    }

    // 8. 处理长按事件：亮LED（仅灯灭时生效）
    if (int_status & TG28_IRQ_LONG) {
        //ws2812_red_open();
        ws2812_blink_red();  
        dev_info(&dev->client->dev, "[TG28 KEY] 长按触发，打开红色LED\n");
        dev->led_flags = 1;
        tg28_clear_irq_bit(dev, TG28_IRQ_LONG);
        tg28_clear_irq_bit(dev, TG28_IRQ_SHORT);
        clear_flag = true;
    }

    // 9. 处理短按事件：双击逻辑（仅灯灭时生效）
    if ((int_status & TG28_IRQ_SHORT) && dev->led_flags == 0) {
        spin_lock_irqsave(&dev->irq_lock, flags);

        if (!dev->is_first_short) {
            // 第一次短按：启动双击检测定时器
            dev->is_first_short = true;
            dev->first_short_jiff = now;
            queue_delayed_work(system_wq, &dev->double_click_dwork, DOUBLE_CLICK_JIFFIES);
            dev_info(&dev->client->dev, "[TG28 KEY] 第一次短按，启动双击检测定时器（%dms）\n",
                    dev->double_click_ms);
        } else {
            // 第二次短按：判断是否在双击窗口内
            if (time_before(now, dev->first_short_jiff + DOUBLE_CLICK_JIFFIES)) {
                // 双击触发：上报重启事件（KEY_RESTART）
                cancel_delayed_work_sync(&dev->double_click_dwork);

                input_report_key(dev->input, KEY_RESTART, 1);
                input_sync(dev->input);
                input_report_key(dev->input, KEY_RESTART, 0);
                input_sync(dev->input);

                dev_info(&dev->client->dev, "[TG28 KEY] 双击触发（间隔%dms），上报重启事件\n",
                        jiffies_to_msecs(now - dev->first_short_jiff));

                // 重置双击状态
                dev->is_first_short = false;
                dev->first_short_jiff = 0;
            } else {
                // 第二次短按超时：视为两次独立短按
                cancel_delayed_work_sync(&dev->double_click_dwork);

                // 上报第一次短按（超时）
                input_report_key(dev->input, KEY_POWER, 1);
                input_sync(dev->input);
                input_report_key(dev->input, KEY_POWER, 0);
                input_sync(dev->input);
                dev_info(&dev->client->dev, "[TG28 KEY] 第一次短按超时，上报单次短按\n");

                // 标记第二次短按为新的第一次
                dev->first_short_jiff = now;
                queue_delayed_work(system_wq, &dev->double_click_dwork, DOUBLE_CLICK_JIFFIES);
                dev_info(&dev->client->dev, "[TG28 KEY] 第二次短按，启动新的双击检测定时器\n");
            }
        }

        tg28_clear_irq_bit(dev, TG28_IRQ_SHORT);
        clear_flag = true;
        spin_unlock_irqrestore(&dev->irq_lock, flags);
    }

    // 10. 处理按键松开事件：灭LED（无论是否长按）
    if (int_status & TG28_IRQ_RELEASE) {
        dev->key_pressed = false;
        if (dev->led_flags) {
            //ws2812_red_close();
            dev_info(&dev->client->dev, "[TG28 KEY] 按键松开，关闭红色LED\n");
            dev->led_flags = 0;
        }
        tg28_clear_irq_bit(dev, TG28_IRQ_RELEASE);
        clear_flag = true;
        dev_info(&dev->client->dev, "[TG28 KEY] 按键松开（重置状态）\n");
    }

    // 11. 恢复中断使能
    //tg28_clear_irq_bit(dev, TG28_IRQ_RELEASE);
out_restore_irq:
    if (clear_flag) {
        tg28_enable_irq(dev);
    } else {
        dev_dbg(&dev->client->dev, "[TG28 WORK] 清除所有中断状态并恢复使能\n");
        tg28_clear_irq_status(dev);
        tg28_enable_irq(dev);
    }
}

/* ========================== 10. 设备树解析与硬件初始化 ========================== */
/**
 * tg28_parse_dt - 解析设备树参数
 * @dev: 设备结构体
 * @tg28: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int tg28_parse_dt(struct device *dev, struct tg28_dev *tg28)
{
    struct device_node *np = dev->of_node;

    // 默认双击阈值
    tg28->double_click_ms = DOUBLE_CLICK_MS;

    // 解析设备树可选参数：tg28,double-click-ms
    of_property_read_u32(np, "tg28,double-click-ms", &tg28->double_click_ms);

    // 解析PowerKey GPIO
    tg28->gpio = of_get_named_gpio_flags(np, "powerkey-gpio", 0, NULL);
    if (!gpio_is_valid(tg28->gpio)) {
        dev_err(dev, "[TG28 DT] GPIO无效：%d\n", tg28->gpio);
        return -EINVAL;
    }

    // 解析中断号
    tg28->irq = irq_of_parse_and_map(np, 0);
    if (tg28->irq <= 0 || tg28->irq == NO_IRQ) {
        dev_err(dev, "[TG28 DT] 中断号无效：%d\n", tg28->irq);
        return -EINVAL;
    }

    dev_info(dev, "[TG28 DT] 解析成功 → GPIO:%d 中断号:%d 双击阈值:%dms\n",
             tg28->gpio, tg28->irq, tg28->double_click_ms);
    return 0;
}

/**
 * tg28_hw_init - TG28硬件初始化
 * @tg28: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int tg28_hw_init(struct tg28_dev *tg28)
{
    int ret;
    u8 pon_level = 0;
    int gpio_value;

    // 1. 申请并配置GPIO为输入
    ret = devm_gpio_request(&tg28->client->dev, tg28->gpio, DRV_NAME "-gpio");
    if (ret) {
        dev_err(&tg28->client->dev, "[TG28 HW] GPIO申请失败，错误码：%d\n", ret);
        return ret;
    }
    gpio_direction_input(tg28->gpio);

    // 2. 备份INTEN1寄存器初始值
    ret = tg28_i2c_read(tg28, TG28_REG_INTEN1, &tg28->inten1_backup);
    if (ret < 0) {
        dev_err(&tg28->client->dev, "[TG28 HW] 备份INTEN1失败，错误码：%d\n", ret);
        return ret;
    }
    dev_info(&tg28->client->dev, "[TG28 HW] 备份INTEN1初始值：0x%02x\n", tg28->inten1_backup);

    // 3. 配置PONLEVEL寄存器：清空长按配置位（bit5-4）
    ret = tg28_i2c_read(tg28, TG28_REG_PONLEVEL, &pon_level);
    if (ret < 0) {
        dev_err(&tg28->client->dev, "[TG28 HW] 读PONLEVEL失败，错误码：%d\n", ret);
        return ret;
    }
    //pon_level &= 0xCF; // 清空bit5-4  1111 0011
    pon_level =(pon_level|(3<<4));//配置长按中断触发时间
    pon_level &= 0xF3; // 清空bit2-3  1111 0011 4  //配置按住电源键关机时间
    ret = tg28_i2c_write(tg28, TG28_REG_PONLEVEL, pon_level);
    if (ret < 0) {
        dev_err(&tg28->client->dev, "[TG28 HW] 写PONLEVEL失败，错误码：%d\n", ret);
        return ret;
    }
    dev_info(&tg28->client->dev, "[TG28 HW] 配置PONLEVEL：0x%02x\n", pon_level);

    // 4. 初始化中断状态
    ret = tg28_disable_irq_full(tg28);
    if (ret < 0) return ret;

    ret = tg28_clear_irq_status(tg28);
    if (ret < 0) return ret;

    ret = tg28_enable_irq(tg28);
    if (ret < 0) return ret;

    // 5. 读取GPIO初始值
    gpio_value = gpio_get_value(tg28->gpio);
    dev_info(&tg28->client->dev, "[TG28 HW] GPIO初始值：%d\n", gpio_value);

    // 6. 申请中断：上升沿+下降沿+ONESHOT（避免嵌套）
    ret = devm_request_threaded_irq(&tg28->client->dev, tg28->irq,
                                   NULL, tg28_irq_handler,
                                   //IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                                   IRQF_TRIGGER_FALLING  | IRQF_ONESHOT,
                                   DRV_NAME, tg28);
    if (ret) {
        dev_err(&tg28->client->dev, "[TG28 HW] 申请中断失败，错误码：%d\n", ret);
        tg28_disable_irq_full(tg28);
        return ret;
    }

    // 7. 初始化按键状态
    tg28->is_first_short = false;
    tg28->first_short_jiff = 0;
    tg28->key_pressed = false;
    tg28->led_flags = 0;
    tg28->last_irq_jiff = jiffies;

    return 0;
}

/**
 * pmic_i2c_reg_config - 配置PMIC电源管理寄存器
 * @tg28: 设备私有数据
 * 返回：0成功，负数内核错误码
 */
static int pmic_i2c_reg_config(struct tg28_dev *tg28)
{
    int ret;
    u8 reg_val;
    struct i2c_client *client = tg28->client;

    // 1. 配置VINDPM输入电压限制：4.36V（0x15=0x06）
    ret = i2c_smbus_write_byte_data(client, 0x15, 0x06);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x15失败，错误码：%d\n", ret);
        return ret;
    }

    // 2. 配置输入限流：500mA（0x16=0x03）
    ret = i2c_smbus_write_byte_data(client, 0x16, 0x03);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x16失败，错误码：%d\n", ret);
        return ret;
    }

    // 3. 预充/快充配置：0x61=0x04（预充100mA）、0x62=0x0B（快充500mA）
    ret = i2c_smbus_write_byte_data(client, 0x61, 0x04);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x61失败，错误码：%d\n", ret);
        return ret;
    }
    ret = i2c_smbus_write_byte_data(client, 0x62, 0x0B);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x62失败，错误码：%d\n", ret);
        return ret;
    }

    // 4. 终止电流配置：读改写0x63（保留高4位，低4位=0x04）
    reg_val = i2c_smbus_read_byte_data(client, 0x63);
    if (reg_val < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 读寄存器0x63失败，错误码：%d\n", reg_val);
        return reg_val;
    }
    reg_val = (reg_val & 0xF0) | 0x04;
    ret = i2c_smbus_write_byte_data(client, 0x63, reg_val);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x63失败，错误码：%d\n", ret);
        return ret;
    }

    // 5. ADC全开：0x30=0x1F
    ret = i2c_smbus_write_byte_data(client, 0x30, 0x1F);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x30失败，错误码：%d\n", ret);
        return ret;
    }

    // 6. 额外配置：0x14=0x05
    ret = i2c_smbus_write_byte_data(client, 0x14, 0x05);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PMIC] 写寄存器0x14失败，错误码：%d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "[TG28 PMIC] 所有寄存器配置成功\n");
    return 0;
}

/* ========================== 11. 驱动Probe/Remove ========================== */
/**
 * tg28_probe - I2C驱动Probe函数（设备匹配时执行）
 * @client: I2C客户端
 * @id: 设备ID表
 * 返回：0成功，负数内核错误码
 */
static int tg28_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct tg28_dev *tg28;
    struct input_dev *input;
    int ret;

    // 检查I2C适配器是否支持SMBUS_BYTE操作
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "[TG28 PROBE] I2C适配器不支持SMBUS_BYTE\n");
        return -EOPNOTSUPP;
    }

    // 分配私有数据
    tg28 = devm_kzalloc(&client->dev, sizeof(*tg28), GFP_KERNEL);
    if (!tg28) {
        dev_err(&client->dev, "[TG28 PROBE] 分配私有数据失败\n");
        return -ENOMEM;
    }
    tg28->client = client;
    i2c_set_clientdata(client, tg28);

    // 初始化锁和工作队列
    mutex_init(&tg28->i2c_lock);
    spin_lock_init(&tg28->irq_lock);
    INIT_WORK(&tg28->irq_work, tg28_irq_work_handler);
    INIT_DELAYED_WORK(&tg28->double_click_dwork, tg28_single_short_work);

    // 解析设备树
    ret = tg28_parse_dt(&client->dev, tg28);
    if (ret) {
        dev_err(&client->dev, "[TG28 PROBE] 解析设备树失败，错误码：%d\n", ret);
        return ret;
    }

    // 初始化输入子系统
    input = devm_input_allocate_device(&client->dev);
    if (!input) {
        dev_err(&client->dev, "[TG28 PROBE] 分配输入设备失败\n");
        return -ENOMEM;
    }

    tg28->input = input;
    input->name = DRV_NAME;
    input->phys = DRV_NAME "/input0";
    input->id.bustype = BUS_I2C;

    // 注册按键事件
    __set_bit(EV_KEY, input->evbit);
    __set_bit(KEY_POWER, input->keybit);    // 单次短按
    __set_bit(KEY_RESTART, input->keybit);  // 双击

    ret = input_register_device(input);
    if (ret) {
        dev_err(&client->dev, "[TG28 PROBE] 注册输入设备失败，错误码：%d\n", ret);
        return ret;
    }

    // 硬件初始化
    ret = tg28_hw_init(tg28);
    if (ret) {
        dev_err(&client->dev, "[TG28 PROBE] 硬件初始化失败，错误码：%d\n", ret);
        input_unregister_device(input);
        return ret;
    }

    // PMIC寄存器配置
    ret = pmic_i2c_reg_config(tg28);
    if (ret < 0) {
        dev_err(&client->dev, "[TG28 PROBE] PMIC配置失败，错误码：%d\n", ret);
        input_unregister_device(input);
        return ret;
    }

    dev_info(&client->dev, "[TG28 PROBE] 驱动初始化成功（版本：%s）\n", DRV_VERSION);
    return 0;
}

/**
 * tg28_remove - I2C驱动Remove函数（设备卸载时执行）
 * @client: I2C客户端
 * 返回：0成功，负数内核错误码
 */
static int tg28_remove(struct i2c_client *client)
{
    struct tg28_dev *tg28 = i2c_get_clientdata(client);

    // 禁用所有中断
    tg28_disable_irq_full(tg28);

    // 停止所有工作队列
    cancel_work_sync(&tg28->irq_work);
    cancel_delayed_work_sync(&tg28->double_click_dwork);

    // 恢复INTEN1寄存器初始值
    if (tg28->inten1_backup != 0) {
        tg28_i2c_write(tg28, TG28_REG_INTEN1, tg28->inten1_backup);
        dev_info(&client->dev, "[TG28 REMOVE] 恢复INTEN1初始值：0x%02x\n", tg28->inten1_backup);
    }

    // 确保LED关闭
    if (tg28->led_flags) {
        //ws2812_red_close();
        tg28->led_flags = 0;
    }

    // 注销输入设备
    input_unregister_device(tg28->input);

    dev_info(&client->dev, "[TG28 REMOVE] 驱动卸载成功\n");
    return 0;
}

/* ========================== 12. 驱动注册 ========================== */
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

/* ========================== 13. 模块信息 ========================== */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TG28 Power Key Driver - 支持短按/双击/长按，长按亮LED松开灭LED");
MODULE_AUTHOR("YUANWANG");
MODULE_VERSION(DRV_VERSION);

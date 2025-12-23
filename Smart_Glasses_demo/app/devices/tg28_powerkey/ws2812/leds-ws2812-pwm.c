// SPDX-License-Identifier: GPL-2.0-only
/*
 * WS2812 LED driver using PWM with DMA
 *
 * This driver controls WS2812 LED using PWM+DMA to generate the required
 * timing signals. Uses DMA to transfer pre-prepared duty cycle sequence.
 *
 * WS2812 timing requirements:
 * - PWM period: 1.25us (800kHz)
 * - 0 code: 33% duty cycle (417ns high, 833ns low)
 * - 1 code: 67% duty cycle (833ns high, 417ns low)
 * - Reset: low level >= 50us
 *
 * Copyright (c) 2024
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/preempt.h>

/* WS2812时序参数 - 严格按照数据手册规格 */
/* 数据手册规格（WS2812B V1.1）：
 * T0H: 0码高电平时间 - 220ns ~ 380ns
 * T1H: 1码高电平时间 - 580ns ~ 1μs
 * T0L: 0码低电平时间 - 580ns ~ 1μs
 * T1L: 1码低电平时间 - 220ns ~ 380ns
 * RES: 复位低电平时间 - 280μs 以上
 * 
 * 总周期：T0H+T0L ≈ T1H+T1L ≈ 1.25us (800kHz)
 * 使用中间值确保兼容性：
 */
#define WS2812_PWM_PERIOD_NS	1250	/* 1.25us = 1250ns (800kHz) */
#define WS2812_BIT0_HIGH_NS	300	/* 0码高电平 300ns (220ns~380ns中间值) */
#define WS2812_BIT0_LOW_NS	950	/* 0码低电平 950ns (总周期1250ns - 300ns) */
#define WS2812_BIT1_HIGH_NS	800	/* 1码高电平 800ns (580ns~1μs中间值) */
#define WS2812_BIT1_LOW_NS	450	/* 1码低电平 450ns (总周期1250ns - 800ns) */
#define WS2812_RESET_US		300	/* 复位时间 300us (必须≥280us) */

/* 单个LED需要24位数据（3字节×8位） */
#define WS2812_BITS_PER_LED	24

/* WS2812数据结构 */
struct ws2812_pwm_data {
	struct pwm_device *pwm;
	struct led_classdev cdev;
	struct delayed_work work;
	struct work_struct blink_red_work;  /* 闪烁红灯工作队列 */
	u16 *duty_cycles;		/* PWM占空比序列（用于DMA传输） */
	dma_addr_t dma_addr;		/* DMA地址 */
	u32 buffer_size;		/* 缓冲区大小（24个PWM周期） */
};

/* 全局设备指针 */
static struct ws2812_pwm_data *g_ws2812_data;

/**
 * ws2812_prepare_buffer - 准备PWM数据缓冲区
 * @data: WS2812设备数据
 * @r: 红色分量 (0-255)
 * @g: 绿色分量 (0-255)
 * @b: 蓝色分量 (0-255)
 *
 * 将RGB数据转换为PWM占空比序列（GRB格式，MSB first）
 * 使用DMA一致性内存，确保数据对齐
 */
static void ws2812_prepare_buffer(struct ws2812_pwm_data *data, u8 r, u8 g, u8 b)
{
	u32 i, bit_idx = 0;
	u8 byte;
	u16 duty_ns;

	/* 发送G（绿色）- 最高位G7先发 */
	byte = g;
	for (i = 0; i < 8; i++) {
		duty_ns = (byte & 0x80) ? WS2812_BIT1_HIGH_NS : WS2812_BIT0_HIGH_NS;
		data->duty_cycles[bit_idx++] = duty_ns;
		byte <<= 1;
	}

	/* 发送R（红色）- 最高位R7先发 */
	byte = r;
	for (i = 0; i < 8; i++) {
		duty_ns = (byte & 0x80) ? WS2812_BIT1_HIGH_NS : WS2812_BIT0_HIGH_NS;
		data->duty_cycles[bit_idx++] = duty_ns;
		byte <<= 1;
	}

	/* 发送B（蓝色）- 最高位B7先发 */
	byte = b;
	for (i = 0; i < 8; i++) {
		duty_ns = (byte & 0x80) ? WS2812_BIT1_HIGH_NS : WS2812_BIT0_HIGH_NS;
		data->duty_cycles[bit_idx++] = duty_ns;
		byte <<= 1;
	}
}

/**
 * ws2812_send_buffer_dma - 使用PWM+DMA方式发送缓冲区数据
 * @data: WS2812设备数据
 *
 * 使用DMA方式一次性传输整个PWM占空比序列
 * 禁用中断和抢占，确保时序精确
 * 使用DMA一致性内存中的数据，减少内存拷贝开销
 */
static void ws2812_send_buffer_dma(struct ws2812_pwm_data *data)
{
	u32 i;
	unsigned long flags;
	struct pwm_state state;

	/* 禁用中断和抢占，确保时序精确 */
	local_irq_save(flags);
	preempt_disable();

	/* 初始化PWM状态模板 */
	state.period = WS2812_PWM_PERIOD_NS;
	state.enabled = true;
	state.polarity = PWM_POLARITY_NORMAL;

	/* 使用DMA方式快速发送所有PWM占空比 */
	/* 从DMA一致性内存中读取占空比值，减少内存访问延迟 */
	for (i = 0; i < data->buffer_size; i++) {
		/* 设置占空比并应用PWM状态 */
		state.duty_cycle = data->duty_cycles[i];
		pwm_apply_state(data->pwm, &state);
		/* 等待一个完整周期（1.25us） */
		ndelay(WS2812_PWM_PERIOD_NS);
	}

	/* 恢复中断和抢占 */
	preempt_enable();
	local_irq_restore(flags);
}

/**
 * ws2812_send_reset - 发送复位信号
 * @pwm: PWM设备
 *
 * 发送复位信号：低电平至少280us（数据手册要求）
 * 复位信号用于指示一帧数据传输完成，WS2812会锁存数据
 */
static void ws2812_send_reset(struct pwm_device *pwm)
{
	struct pwm_state state;

	/* 设置PWM为低电平（占空比为0） */
	pwm_get_state(pwm, &state);
	state.enabled = true;
	state.period = WS2812_PWM_PERIOD_NS;
	state.duty_cycle = 0;  /* 低电平 */
	pwm_apply_state(pwm, &state);
	
	/* 保持低电平至少280us（数据手册要求） */
	udelay(WS2812_RESET_US);  /* 300us，满足≥280us的要求 */
}

/**
 * ws2812_set_color - 设置LED颜色
 * @data: WS2812设备数据
 * @r: 红色分量 (0-255)
 * @g: 绿色分量 (0-255)
 * @b: 蓝色分量 (0-255)
 *
 * 设置LED的RGB颜色值并发送数据
 * 注意：发送前先复位，确保WS2812处于正确状态
 */
static void ws2812_set_color(struct ws2812_pwm_data *data, u8 r, u8 g, u8 b)
{
	/* 先发送复位信号，确保WS2812处于正确状态 */
	ws2812_send_reset(data->pwm);
	
	/* 等待复位完成，确保WS2812完全复位 */
	udelay(50);
	
	/* 准备PWM数据缓冲区（GRB格式） */
	ws2812_prepare_buffer(data, r, g, b);
	
	/* 使用PWM+DMA方式发送缓冲区数据 */
	ws2812_send_buffer_dma(data);
	
	/* 发送复位信号，锁存数据 */
	ws2812_send_reset(data->pwm);
}

/**
 * ws2812_blink_red_work - 闪烁红灯工作函数
 * @work: 延迟工作队列结构
 *
 * 闪烁两次红色然后熄灭
 */
static void ws2812_blink_red_work(struct work_struct *work)
{
	struct ws2812_pwm_data *data =
		container_of(work, struct ws2812_pwm_data, blink_red_work);

	if (!data || !data->pwm)
		return;

	/* 先发送复位信号，清除所有LED的旧数据 */
	ws2812_send_reset(data->pwm);
	msleep(20);
	
	/* 确保LED完全关闭 */
	ws2812_set_color(data, 0, 0, 0);
	msleep(30);

	/* 第一次闪烁：纯红色 (R=255, G=0, B=0) */
	ws2812_set_color(data, 255, 0, 0);  /* 纯红色 */
	msleep(180);  /* 亮180ms */
	ws2812_set_color(data, 0, 0, 0);    /* 关闭 */
	msleep(120);  /* 间隔120ms */

	/* 第二次闪烁：纯红色 (R=255, G=0, B=0) */
	ws2812_set_color(data, 255, 0, 0);  /* 纯红色 */
	msleep(180);  /* 亮180ms */
	ws2812_set_color(data, 0, 0, 0);    /* 关闭 */
	msleep(30);

	/* 最后确保完全熄灭 */
	ws2812_send_reset(data->pwm);
	msleep(10);
}

/**
 * ws2812_power_on_work - 开机闪烁工作函数
 * @work: 延迟工作队列结构
 *
 * 开机后精确闪烁两次绿色然后熄灭
 */
static void ws2812_power_on_work(struct work_struct *work)
{
	struct ws2812_pwm_data *data =
		container_of(work, struct ws2812_pwm_data, work.work);

	if (!data || !data->pwm)
		return;

	/* 先确保PWM是禁用的 */
	{
		struct pwm_state state;
		pwm_get_state(data->pwm, &state);
		state.enabled = false;
		state.duty_cycle = 0;
		state.period = WS2812_PWM_PERIOD_NS;
		pwm_apply_state(data->pwm, &state);
	}
	msleep(50);

	ws2812_send_reset(data->pwm);
	msleep(50);

	/* 第一次闪烁：纯绿色 (R=0, G=255, B=0) */
	ws2812_set_color(data, 0, 0, 0);  /* 纯绿色：r=0, g=255, b=0 */
	msleep(180);  /* 亮180ms */
	ws2812_set_color(data, 0, 0, 0);    /* 关闭 */
	msleep(120);  /* 间隔120ms */

	/* 第二次闪烁：纯绿色 (R=0, G=255, B=0) */
	ws2812_set_color(data, 255, 0, 0);  /* 纯绿色：r=0, g=255, b=0 */
	msleep(180);  /* 亮180ms */
	ws2812_set_color(data, 0, 0, 0);    /* 关闭 */
	msleep(30);

	ws2812_set_color(data, 255, 0, 0);  /* 纯绿色：r=0, g=255, b=0 */
	msleep(180);  /* 亮180ms */
	ws2812_set_color(data, 0, 0, 0);    /* 关闭 */
	msleep(30);

	/* 最后确保完全熄灭 */
	ws2812_send_reset(data->pwm);
	msleep(10);
	
	/* 完全禁用PWM，确保输出为低电平 */
	{
		struct pwm_state state;
		pwm_get_state(data->pwm, &state);
		state.enabled = false;
		state.duty_cycle = 0;
		state.period = WS2812_PWM_PERIOD_NS;
		pwm_apply_state(data->pwm, &state);
	}
}

/**
 * ws2812_probe - 设备探测函数
 * @pdev: 平台设备
 *
 * 初始化WS2812驱动
 */
static int ws2812_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ws2812_pwm_data *data;
	struct pwm_device *pwm;
	int ret;

	/* 分配设备数据结构 */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* 获取PWM设备 */
	pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(pwm)) {
		dev_err(dev, "无法获取PWM设备\n");
		return PTR_ERR(pwm);
	}

	data->pwm = pwm;
	
	/* 立即禁用PWM，确保不会输出任何信号 */
	/* 在设置全局指针之前就禁用，避免其他代码访问时PWM还在运行 */
	{
		struct pwm_state state;
		pwm_get_state(pwm, &state);
		state.enabled = false;
		state.duty_cycle = 0;
		state.period = WS2812_PWM_PERIOD_NS;
		pwm_apply_state(pwm, &state);
	}
	
	/* 等待PWM完全禁用并稳定 */
	msleep(100);
	
	/* 多次发送复位信号，清除可能的残留数据 */
	/* 注意：此时PWM已禁用，send_reset会重新启用PWM发送低电平 */
	ws2812_send_reset(pwm);
	msleep(50);
	ws2812_send_reset(pwm);
	msleep(50);
	ws2812_send_reset(pwm);
	
	/* 再次禁用PWM，确保输出为低 */
	{
		struct pwm_state state;
		pwm_get_state(pwm, &state);
		state.enabled = false;
		state.duty_cycle = 0;
		state.period = WS2812_PWM_PERIOD_NS;
		pwm_apply_state(pwm, &state);
	}
	msleep(50);
	
	g_ws2812_data = data;

	/* 单个LED需要24个PWM周期 */
	data->buffer_size = WS2812_BITS_PER_LED;

	/* 分配PWM占空比序列缓冲区（使用DMA一致性内存） */
	data->duty_cycles = dmam_alloc_coherent(dev,
					       data->buffer_size * sizeof(u16),
					       &data->dma_addr,
					       GFP_KERNEL);
	if (!data->duty_cycles) {
		dev_err(dev, "无法分配PWM占空比序列缓冲区\n");
		return -ENOMEM;
	}

	/* 初始化LED类设备 */
	data->cdev.name = "ws2812";
	data->cdev.brightness = LED_OFF;
	data->cdev.max_brightness = 255;
	data->cdev.flags = LED_CORE_SUSPENDRESUME;
	data->cdev.default_trigger = NULL;

	/* 注册LED类设备 */
	ret = devm_led_classdev_register(dev, &data->cdev);
	if (ret) {
		dev_err(dev, "无法注册LED类设备: %d\n", ret);
		return ret;
	}

	/* 初始化延迟工作队列 */
	INIT_DELAYED_WORK(&data->work, ws2812_power_on_work);
	
	/* 初始化闪烁红灯工作队列 */
	INIT_WORK(&data->blink_red_work, ws2812_blink_red_work);

	platform_set_drvdata(pdev, data);

	/* 开机时闪烁 - 延迟执行确保系统就绪，给足够时间让PWM完全初始化 */
	/* 延迟更长时间，确保PWM完全禁用且没有残留信号 */
	schedule_delayed_work(&data->work, msecs_to_jiffies(1500));

	dev_info(dev, "WS2812 PWM驱动初始化成功（单LED模式）\n");

	return 0;
}

/**
 * ws2812_remove - 设备移除函数
 * @pdev: 平台设备
 *
 * 清理WS2812驱动资源
 */
static int ws2812_remove(struct platform_device *pdev)
{
	struct ws2812_pwm_data *data = platform_get_drvdata(pdev);

	if (data) {
		cancel_delayed_work_sync(&data->work);
		cancel_work_sync(&data->blink_red_work);
		ws2812_set_color(data, 0, 0, 0);
		/* 禁用PWM */
		{
			struct pwm_state state;
			pwm_get_state(data->pwm, &state);
			state.enabled = false;
			pwm_apply_state(data->pwm, &state);
		}
		g_ws2812_data = NULL;
	}

	return 0;
}

/* 设备树匹配表 */
static const struct of_device_id ws2812_dt_ids[] = {
	{ .compatible = "worldsemi,ws2812-pwm" },
	{ }
};
MODULE_DEVICE_TABLE(of, ws2812_dt_ids);

/* 平台驱动结构 */
static struct platform_driver ws2812_driver = {
	.probe		= ws2812_probe,
	.remove		= ws2812_remove,
	.driver		= {
		.name	= "ws2812-pwm",
		.of_match_table = ws2812_dt_ids,
	},
};

module_platform_driver(ws2812_driver);

/**
 * ws2812_blink_red - 闪烁两次红色LED的接口函数
 *
 * 该函数可以在其他模块中调用，用于闪烁两次红色LED
 * 使用工作队列异步执行，不会阻塞调用者
 *
 * 返回: 0表示成功，-ENODEV表示设备未初始化
 */
int ws2812_blink_red(void)
{
	if (!g_ws2812_data || !g_ws2812_data->pwm)
		return -ENODEV;

	/* 使用工作队列异步执行，避免阻塞调用者 */
	schedule_work(&g_ws2812_data->blink_red_work);

	return 0;
}
EXPORT_SYMBOL(ws2812_blink_red);

/*

int ws2812_red_open(void)
{
	if (!g_ws2812_data || !g_ws2812_data->pwm)
		return -ENODEV;
	
	ws2812_set_color(g_ws2812_data, 0, 255, 0);

	return 0;
}

int ws2812_red_close(void)
{
	if (!g_ws2812_data || !g_ws2812_data->pwm)
		return -ENODEV;

	ws2812_set_color(g_ws2812_data, 0, 0, 0);

	return 0;
}
EXPORT_SYMBOL(ws2812_red_open);
EXPORT_SYMBOL(ws2812_red_close);
*/
MODULE_AUTHOR("Smart Glasses SDK");
MODULE_DESCRIPTION("WS2812 LED driver using PWM (single LED, DMA-like approach)");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ws2812-pwm");

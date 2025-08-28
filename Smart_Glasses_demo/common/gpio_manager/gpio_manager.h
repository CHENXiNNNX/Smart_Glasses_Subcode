#ifndef GPIO_MANAGER_H
#define GPIO_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../conf/dev_conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义常用的GPIO引脚
#define LED_BLUE     calculate_gpio_pin(0, 0, 4)  // GPIO0_A4
#define MOTOR1_INA   calculate_gpio_pin(1, 0, 0)  // GPIO1_A0
#define MOTOR1_INB   calculate_gpio_pin(1, 0, 1)  // GPIO1_A1
#define MOTOR2_INA   calculate_gpio_pin(1, 0, 3)  // GPIO1_A3
#define MOTOR2_INB   calculate_gpio_pin(1, 0, 4)  // GPIO1_A4

#define OUT_DIRECTION "out"
#define IN_DIRECTION  "in"

// 根据bank, group, X计算GPIO编号
int calculate_gpio_pin(int bank, int group, int x);

// 导出GPIO引脚
int gpio_export(int gpio_pin);

// 清理GPIO引脚（释放资源）
int gpio_unexport(int gpio_pin);

// 设置GPIO引脚方向（输入或输出）
int gpio_set_direction(int gpio_pin, const char *direction);

// 设置GPIO引脚值（仅适用于输出模式）
int gpio_set_value(int gpio_pin, int value);

// 读取GPIO引脚值（仅适用于输入模式）
int gpio_get_value(int gpio_pin);

// 初始化GPIO引脚
void gpio_init(int gpio_pin, const char *direction);

// deinit GPIO引脚
void gpio_deinit(int gpio_pin);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
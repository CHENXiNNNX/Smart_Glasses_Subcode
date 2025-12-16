
#include"icp20100.h"


#include <stdio.h>
#include <unistd.h>  // 增加sleep所需的头文件
// 主函数示例
int main(void) {
    icp20100_dev_t dev = {0};  // 初始化设备句柄（路径NULL，状态关闭）
    float pressure, temperature;
    int ret;

    // 1. 打开设备
    ret = icp20100_open(&dev);
    if (ret != 0) {
        fprintf(stderr, "main: Failed to open ICP20100 device\n");
        return 1;
    }

    // 2. 循环读取数据
    while (1) {
        // 读取气压
        ret = icp20100_read_pressure(&dev, &pressure);
        if (ret != 0) {
            fprintf(stderr, "main: Failed to read pressure\n");
            break;
        }

        // 读取温度
        ret = icp20100_read_temperature(&dev, &temperature);
        if (ret != 0) {
            fprintf(stderr, "main: Failed to read temperature\n");
            break;
        }

        printf("Pressure: %.3f kPa, Temperature: %.3f °C\n", pressure, temperature);
        sleep(1);
    }

    // 3. 关闭设备（无论是否读取失败，都要释放资源）
    icp20100_close(&dev);
    return ret;
}
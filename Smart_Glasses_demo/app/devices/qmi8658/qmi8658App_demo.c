#include "qmi8658.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h> 

static int exit_flag = 0;

static void sigint_handler(int sig) {
    if (sig == SIGINT) {
        printf("\nReceived Ctrl+C, exiting...\n");
        exit_flag = 1;
    }
}

int main(void) {
    QMI8658_DevHandle *handle = NULL;
    QMI8658_SensorData acc_data, gyro_data;
    QMI8658_AngleData angle;
    int acc_ret, gyro_ret;

    signal(SIGINT, sigint_handler);

    // 打开传感器
    handle = qmi8658_open();
    if (!handle) {
        fprintf(stderr, "Open QMI8658 failed: %s\n", qmi8658_get_error());
        return -1;
    }
    printf("QMI8658 open success\n");

    // 初始化数据结构体
    memset(&acc_data, 0, sizeof(acc_data));
    memset(&gyro_data, 0, sizeof(gyro_data));

    printf("Reading sensor data (Ctrl+C to exit)...\n");
    while (!exit_flag) {
        // 读取加速度计（区分「成功」「无数据」「错误」）
        acc_ret = qmi8658_read_accelerometer(handle, &acc_data);
        if (acc_ret == QMI8658_SUCCESS) {
            printf("Accelerometer: X=%d mg, Y=%d mg, Z=%d mg [VALID]\n", 
                   acc_data.x, acc_data.y, acc_data.z);
        } else if (acc_ret == QMI8658_NO_DATA) {
            printf("Accelerometer: X=%d mg, Y=%d mg, Z=%d mg [HISTORY]\n", 
                   acc_data.x, acc_data.y, acc_data.z); // 更友好的提示
        } else {
            fprintf(stderr, "Accelerometer: [ERROR] %s\n", qmi8658_get_error());
        }

        // 读取陀螺仪
        gyro_ret = qmi8658_read_gyroscope(handle, &gyro_data);
        if (gyro_ret == QMI8658_SUCCESS) {
            printf("Gyroscope:     X=%d dps, Y=%d dps, Z=%d dps [VALID]\n", 
                   gyro_data.x, gyro_data.y, gyro_data.z);
        } else if (gyro_ret == QMI8658_NO_DATA) {
            printf("Gyroscope:     X=%d dps, Y=%d dps, Z=%d dps [HISTORY]\n", 
                   gyro_data.x, gyro_data.y, gyro_data.z); // 更友好的提示
        } else {
            fprintf(stderr, "Gyroscope:     [ERROR] %s\n", qmi8658_get_error());
        }

        // 计算角度（自动兼容历史数据）
        angle = qmi8658_calculate_angle(&acc_data);
        if (angle.valid) {
            printf("Angle:         Pitch=%.2f°, Roll=%.2f° [VALID]\n", angle.pitch, angle.roll);
        } else {
            printf("Angle:         Pitch=%.2f°, Roll=%.2f° [INVALID]\n", angle.pitch, angle.roll);
        }
        printf("----------------------------------------\n");

        usleep(100000); // 100ms读取一次（平衡采样率和NO DATA概率）
    }

    // 关闭传感器
    qmi8658_close(handle);
    printf("QMI8658 closed\n");

    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include"icp20100.h"



/**
 * @brief 打开ICP20100设备（封装查找逻辑，返回设备句柄）
 * @param dev 设备句柄指针（需提前分配内存）
 * @return 0:成功，-1:失败
 */
int icp20100_open(icp20100_dev_t *dev) {
    DIR *dir;
    struct dirent *entry;
    char path[256];
    char buf[64];
    int fd;
    ssize_t len;

    // 参数校验
    if (dev == NULL) {
        fprintf(stderr, "icp20100_open: dev pointer is NULL\n");
        return -1;
    }

    // 防止重复打开
    if (dev->is_open) {
        fprintf(stderr, "icp20100_open: device already opened\n");
        return -1;
    }

    // 打开IIO设备目录
    dir = opendir(IIO_BASE_PATH);
    if (!dir) {
        perror("icp20100_open: Failed to open IIO base directory");
        return -1;
    }

    // 遍历所有IIO设备
    while ((entry = readdir(dir)) != NULL) {
        // 只处理"iio:deviceX"格式的目录
        if (strncmp(entry->d_name, "iio:device", 10) != 0)
            continue;

        // 拼接"name"属性文件路径
        snprintf(path, sizeof(path), "%s/%s/name", IIO_BASE_PATH, entry->d_name);
        
        // 读取"name"属性判断是否为目标设备
        fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        len = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (len <= 0)
            continue;

        buf[len] = '\0';
        // 移除换行符
        if (buf[len - 1] == '\n')
            buf[len - 1] = '\0';

        // 匹配设备名称
        if (strcmp(buf, DEVICE_NAME) == 0) {
            // 分配内存存储设备路径
            dev->device_path = malloc(strlen(IIO_BASE_PATH) + strlen(entry->d_name) + 2);
            if (!dev->device_path) {
                perror("icp20100_open: Failed to allocate memory");
                closedir(dir);
                return -1;
            }
            snprintf(dev->device_path, strlen(IIO_BASE_PATH) + strlen(entry->d_name) + 2,
                     "%s/%s", IIO_BASE_PATH, entry->d_name);
            dev->is_open = 1;  // 标记设备已打开
            closedir(dir);
            printf("icp20100_open: Found device at %s\n", dev->device_path);
            return 0;
        }
    }

    closedir(dir);
    fprintf(stderr, "icp20100_open: ICP20100 device not found in %s\n", IIO_BASE_PATH);
    return -1;
}

/**
 * @brief 关闭ICP20100设备（释放资源，重置句柄）
 * @param dev 设备句柄指针
 * @return 0:成功，-1:失败
 */
int icp20100_close(icp20100_dev_t *dev) {
    // 参数校验
    if (dev == NULL) {
        fprintf(stderr, "icp20100_close: dev pointer is NULL\n");
        return -1;
    }

    // 防止重复关闭
    if (!dev->is_open) {
        fprintf(stderr, "icp20100_close: device not opened\n");
        return -1;
    }

    // 释放设备路径内存
    if (dev->device_path) {
        free(dev->device_path);
        dev->device_path = NULL;
    }

    // 重置状态
    dev->is_open = 0;
    printf("icp20100_close: Device closed successfully\n");
    return 0;
}

/**
 * @brief 读取IIO设备的浮点属性值（内部接口）
 * @param dev 设备句柄
 * @param attr 属性名（如"in_pressure_input"）
 * @param value 输出：读取到的浮点值
 * @return 0:成功，-1:失败
 */
static int read_iio_float_attribute(const icp20100_dev_t *dev, const char *attr, float *value) {
    char path[256];
    char buf[64];
    int fd;
    ssize_t len;

    // 参数校验
    if (dev == NULL || !dev->is_open || attr == NULL || value == NULL) {
        fprintf(stderr, "read_iio_float_attribute: invalid parameters\n");
        return -1;
    }

    // 拼接属性文件路径
    snprintf(path, sizeof(path), "%s/%s", dev->device_path, attr);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("read_iio_float_attribute: Failed to open attribute file");
        return -1;
    }

    // 读取属性值
    len = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (len <= 0) {
        perror("read_iio_float_attribute: Failed to read attribute");
        return -1;
    }
    buf[len] = '\0';

    // 解析浮点数值
    if (sscanf(buf, "%f", value) != 1) {
        fprintf(stderr, "read_iio_float_attribute: Failed to parse float value: %s\n", buf);
        return -1;
    }

    return 0;
}

/**
 * @brief 读取ICP20100气压值
 * @param dev 设备句柄
 * @param pressure 输出：气压值（kPa）
 * @return 0:成功，-1:失败
 */
int icp20100_read_pressure(const icp20100_dev_t *dev, float *pressure) {
    return read_iio_float_attribute(dev, "in_pressure_input", pressure);
}

/**
 * @brief 读取ICP20100温度值
 * @param dev 设备句柄
 * @param temperature 输出：温度值（°C）
 * @return 0:成功，-1:失败
 */
int icp20100_read_temperature(const icp20100_dev_t *dev, float *temperature) {
    return read_iio_float_attribute(dev, "in_temp_input", temperature);
}


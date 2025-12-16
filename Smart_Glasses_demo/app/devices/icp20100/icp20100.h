#ifndef _ICP20100_H
#define _ICP20100_H

#define IIO_BASE_PATH "/sys/bus/iio/devices"
#define DEVICE_NAME "icp20100"

// 定义ICP20100设备句柄（封装设备路径，便于管理）
typedef struct {
    char *device_path;  // 设备完整路径
    int is_open;        // 设备是否已打开（0:关闭，1:打开）
} icp20100_dev_t;


/**
 * @brief 打开ICP20100设备（封装查找逻辑，返回设备句柄）
 * @param dev 设备句柄指针（需提前分配内存）
 * @return 0:成功，-1:失败
 */
 int icp20100_open(icp20100_dev_t *dev);


 /**
 * @brief 关闭ICP20100设备（释放资源，重置句柄）
 * @param dev 设备句柄指针
 * @return 0:成功，-1:失败
 */
int icp20100_close(icp20100_dev_t *dev);

/**
 * @brief 读取IIO设备的浮点属性值（内部接口）
 * @param dev 设备句柄
 * @param attr 属性名（如"in_pressure_input"）
 * @param value 输出：读取到的浮点值
 * @return 0:成功，-1:失败
 */
 static int read_iio_float_attribute(const icp20100_dev_t *dev, const char *attr, float *value);


 /**
 * @brief 读取ICP20100气压值
 * @param dev 设备句柄
 * @param pressure 输出：气压值（kPa）
 * @return 0:成功，-1:失败
 */
int icp20100_read_pressure(const icp20100_dev_t *dev, float *pressure);


/**
 * @brief 读取ICP20100温度值
 * @param dev 设备句柄
 * @param temperature 输出：温度值（°C）
 * @return 0:成功，-1:失败
 */
 int icp20100_read_temperature(const icp20100_dev_t *dev, float *temperature);
#endif
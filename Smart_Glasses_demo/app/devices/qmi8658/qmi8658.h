#ifndef QMI8658_H
#define QMI8658_H

#include <stdint.h>

// 内置设备名称宏
#define QMI8658_ACC_NAME "accelerometer"
#define QMI8658_GYRO_NAME "gyrocope" 

// 
#ifndef PI
#define PI 3.14159265358979323846
#endif

// 返回值定义（新增：无数据但无错误）
#define QMI8658_SUCCESS      0  // 成功获取有效数据
#define QMI8658_NO_DATA      1  // 无数据（临时超时，非错误）
#define QMI8658_FAILED       2  // 真错误（设备异常/参数错误）

// 传感器原始数据结构体（新增有效性标记）
typedef struct {
    int32_t x;          // 加速度：mg；陀螺仪：dps
    int32_t y;
    int32_t z;
    int valid;          // 1=数据有效，0=数据无效（无新数据时保留上次值）
} QMI8658_SensorData;

// 角度数据结构体
typedef struct {
    float pitch;        // 俯仰角（°）
    float roll;         // 横滚角（°）
    int valid;          // 1=角度有效，0=角度无效
} QMI8658_AngleData;

// 设备句柄（隐藏实现）
typedef struct QMI8658_DevHandle QMI8658_DevHandle;

/************************ 对外接口 ************************/
/**
 * @brief 打开传感器（自动查找设备，单设备也能工作）
 * @return 句柄/NULL
 */
QMI8658_DevHandle* qmi8658_open(void);

/**
 * @brief 关闭传感器
 * @param handle 设备句柄
 */
void qmi8658_close(QMI8658_DevHandle *handle);

/**
 * @brief 读取加速度计（无数据时保留上次值，不返回错误）
 * @param handle 句柄
 * @param data 输出数据（含有效性标记）
 * @return QMI8658_SUCCESS/QMI8658_NO_DATA/QMI8658_FAILED
 */
int qmi8658_read_accelerometer(QMI8658_DevHandle *handle, QMI8658_SensorData *data);

/**
 * @brief 读取陀螺仪（无数据时保留上次值）
 * @param handle 句柄
 * @param data 输出数据
 * @return QMI8658_SUCCESS/QMI8658_NO_DATA/QMI8658_FAILED
 */
int qmi8658_read_gyroscope(QMI8658_DevHandle *handle, QMI8658_SensorData *data);

/**
 * @brief 计算角度（兼容无效/历史加速度数据）
 * @param acc_data 加速度数据
 * @return 角度数据（含有效性标记）
 */
QMI8658_AngleData qmi8658_calculate_angle(const QMI8658_SensorData *acc_data);

/**
 * @brief 获取错误信息（仅QMI8658_FAILED时有效）
 * @return 错误描述
 */
const char* qmi8658_get_error(void);

#endif // QMI8658_H
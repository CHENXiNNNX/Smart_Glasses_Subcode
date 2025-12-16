#include "qmi8658.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>

// 扩展设备句柄：保存上次有效数据
struct QMI8658_DevHandle {
    int acc_fd;                // 加速度计文件描述符
    int gyro_fd;               // 陀螺仪文件描述符
    char acc_path[64];         // 加速度计路径
    char gyro_path[64];        // 陀螺仪路径
    int last_error;            // 最近错误码
    char error_msg[128];       // 最近错误信息
    QMI8658_SensorData last_acc; // 上次加速度有效数据
    QMI8658_SensorData last_gyro;// 上次陀螺仪有效数据
};

// 全局错误缓存
static char g_error_msg[128] = {0};

/************************ 内部工具函数 ************************/
static void set_error(QMI8658_DevHandle *handle, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if (handle) {
        vsnprintf(handle->error_msg, sizeof(handle->error_msg), fmt, args);
        handle->last_error = errno;
    }
    vsnprintf(g_error_msg, sizeof(g_error_msg), fmt, args);

    va_end(args);
}

static char* find_input_dev_by_name(const char *dev_name) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    char *dev_path = NULL;
    char name[256] = {0};
    int fd = -1, ret = -1;

    dir = opendir("/dev/input");
    if (!dir) {
        set_error(NULL, "Open /dev/input failed: %s", strerror(errno));
        return NULL;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        char path[64] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        fd = open(path, O_RDONLY);
        if (fd < 0) {
            continue;
        }

        ret = ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        close(fd);
        if (ret < 0) {
            continue;
        }
        name[ret] = '\0';

        if (strcmp(name, dev_name) == 0) {
            dev_path = strdup(path);
            break;
        }
    }

    closedir(dir);

    if (!dev_path) {
        set_error(NULL, "Device not found: %s", dev_name);
    }

    return dev_path;
}

/**
 * 核心修改：增加短重试+非阻塞读取，无数据时返回上次值
 */
static int read_sensor_data(int fd, QMI8658_SensorData *last_data, QMI8658_SensorData *data) {
    if (fd < 0 || !last_data || !data) {
        set_error(NULL, "Invalid param: fd=%d, data=%p", fd, data);
        return QMI8658_FAILED;
    }

    // 初始化输出数据为上次有效值
    *data = *last_data;
    data->valid = 0; // 默认标记为无效

    struct input_event ev;
    ssize_t n;
    int got_x = 0, got_y = 0, got_z = 0;
    QMI8658_SensorData new_data = {0};

    // 保存原文件状态，设置非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        set_error(NULL, "Set non-block failed: %s", strerror(errno));
        return QMI8658_FAILED;
    }

    // 增加短时间重试（最多50ms），减少NO DATA概率
    int retry = 0;
    const int max_retry = 5; // 5*10ms=50ms
    while (retry < max_retry) {
        while ((n = read(fd, &ev, sizeof(struct input_event))) > 0) {
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                    case ABS_X:
                        new_data.x = ev.value;
                        got_x = 1;
                        break;
                    case ABS_Y:
                        new_data.y = ev.value;
                        got_y = 1;
                        break;
                    case ABS_Z:
                        new_data.z = ev.value;
                        got_z = 1;
                        break;
                }
            } else if (ev.type == EV_SYN) {
                // 同步事件：一帧数据结束
                if (got_x && got_y && got_z) {
                    // 获取到有效数据：更新输出+上次值+标记有效
                    *data = new_data;
                    data->valid = 1;
                    *last_data = new_data; // 保存为下次的上次值
                    fcntl(fd, F_SETFL, flags);
                    return QMI8658_SUCCESS;
                }
                got_x = got_y = got_z = 0;
            }
        }

        if (n < 0 && errno == EAGAIN) {
            usleep(10000); // 10ms重试一次
            retry++;
            continue;
        } else {
            break;
        }
    }

    // 恢复文件状态
    fcntl(fd, F_SETFL, flags);

    // 处理read结果
    if (n < 0) {
        if (errno == EAGAIN) {
            // 无数据（正常场景）：返回上次值，标记无效
            return QMI8658_NO_DATA;
        } else {
            // 真错误（如设备关闭）
            set_error(NULL, "Read failed: %s", strerror(errno));
            return QMI8658_FAILED;
        }
    }

    // 读取到数据但不完整：返回上次值
    return QMI8658_NO_DATA;
}

/************************ 对外接口实现 ************************/
QMI8658_DevHandle* qmi8658_open(void) {
    char *acc_path = find_input_dev_by_name(QMI8658_ACC_NAME);
    char *gyro_path = find_input_dev_by_name(QMI8658_GYRO_NAME);

    // 优先分配句柄，提高容错性（单设备也能工作）
    QMI8658_DevHandle *handle = (QMI8658_DevHandle*)malloc(sizeof(QMI8658_DevHandle));
    if (!handle) {
        set_error(NULL, "Malloc handle failed: %s", strerror(errno));
        free(acc_path);
        free(gyro_path);
        return NULL;
    }
    memset(handle, 0, sizeof(QMI8658_DevHandle));
    handle->acc_fd = -1;
    handle->gyro_fd = -1;
    handle->last_acc.valid = 0;
    handle->last_gyro.valid = 0;

    // 单独处理加速度计打开
    if (acc_path) {
        handle->acc_fd = open(acc_path, O_RDONLY);
        if (handle->acc_fd < 0) {
            set_error(handle, "Open acc %s failed: %s", acc_path, strerror(errno));
        } else {
            strncpy(handle->acc_path, acc_path, sizeof(handle->acc_path) - 1);
        }
        free(acc_path);
    } else {
        set_error(handle, "Acc device not found: %s", QMI8658_ACC_NAME);
    }

    // 单独处理陀螺仪打开
    if (gyro_path) {
        handle->gyro_fd = open(gyro_path, O_RDONLY);
        if (handle->gyro_fd < 0) {
            set_error(handle, "Open gyro %s failed: %s", gyro_path, strerror(errno));
        } else {
            strncpy(handle->gyro_path, gyro_path, sizeof(handle->gyro_path) - 1);
        }
        free(gyro_path);
    } else {
        set_error(handle, "Gyro device not found: %s", QMI8658_GYRO_NAME);
    }

    // 至少一个设备打开成功则返回句柄
    if (handle->acc_fd >= 0 || handle->gyro_fd >= 0) {
        set_error(handle, "Open partial success (acc:%d, gyro:%d)", 
                 handle->acc_fd >= 0, handle->gyro_fd >= 0);
        return handle;
    } else {
        // 所有设备打开失败，释放句柄
        free(handle);
        set_error(NULL, "All devices open failed");
        return NULL;
    }
}

void qmi8658_close(QMI8658_DevHandle *handle) {
    if (handle) {
        if (handle->acc_fd >= 0) {
            close(handle->acc_fd);
        }
        if (handle->gyro_fd >= 0) {
            close(handle->gyro_fd);
        }
        free(handle);
    }
}

int qmi8658_read_accelerometer(QMI8658_DevHandle *handle, QMI8658_SensorData *data) {
    if (!handle || !data || handle->acc_fd < 0) {
        set_error(handle, "Invalid param or acc not open");
        return QMI8658_FAILED;
    }
    return read_sensor_data(handle->acc_fd, &handle->last_acc, data);
}

int qmi8658_read_gyroscope(QMI8658_DevHandle *handle, QMI8658_SensorData *data) {
    if (!handle || !data || handle->gyro_fd < 0) {
        set_error(handle, "Invalid param or gyro not open");
        return QMI8658_FAILED;
    }
    return read_sensor_data(handle->gyro_fd, &handle->last_gyro, data);
}

QMI8658_AngleData qmi8658_calculate_angle(const QMI8658_SensorData *acc_data) {
    QMI8658_AngleData angle = {0.0f, 0.0f, 0};
    // 仅指针为空时返回无效角度，NO DATA（valid=0）仍计算历史值
    if (!acc_data) {
        set_error(NULL, "Acc data NULL, skip angle calc");
        return angle;
    }

    // 即使是历史数据（valid=0），也计算角度
    float ax = acc_data->x / 1000.0f;
    float ay = acc_data->y / 1000.0f;
    float az = acc_data->z / 1000.0f;

    // 避免除零异常
    float sqrt_val = sqrt(ay * ay + az * az);
    if (sqrt_val < 0.0001f) {
        set_error(NULL, "Acc data zero, skip angle calc");
        return angle;
    }

    // 计算角度
    angle.pitch = atan2(ax, sqrt_val) * 180.0f / PI;
    angle.roll = atan2(ay, az) * 180.0f / PI;

    // 角度归一化
    angle.pitch = fmod(angle.pitch, 360.0f);
    if (angle.pitch > 90.0f) {
        angle.pitch = 180.0f - angle.pitch;
    } else if (angle.pitch < -90.0f) {
        angle.pitch = -180.0f - angle.pitch;
    }

    angle.roll = fmod(angle.roll, 360.0f);
    if (angle.roll > 180.0f) {
        angle.roll -= 360.0f;
    } else if (angle.roll < -180.0f) {
        angle.roll += 360.0f;
    }

    angle.valid = 1; // 标记角度有效（只要有数据就有效）
    return angle;
}

const char* qmi8658_get_error(void) {
    return g_error_msg;
}
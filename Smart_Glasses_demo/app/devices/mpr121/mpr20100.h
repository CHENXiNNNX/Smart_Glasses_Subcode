#ifndef __MPR20100_H
#define __MPR20100_H

/************************ 全局配置与类型定义（对外可见） ************************/
// MPR121设备名称（与驱动中input_dev->name一致）
#define MPR121_DEV_NAME "Freescale MPR121 Touchkey"
// 最大触摸通道数（与驱动中MPR121_MAX_KEY_COUNT一致）
#define MPR121_MAX_CHANNEL 12

// 触摸事件类型枚举（对外接口用）
typedef enum {
    MPR121_EVENT_NONE,        // 无事件
    MPR121_EVENT_PRESSED,     // 按下事件
    MPR121_EVENT_RELEASED     // 释放事件
} MPR121_EventType;

// 触摸事件结构体（对外输出事件数据）
typedef struct {
    int channel;              // 触摸通道（0~11）
    int key_code;             // 按键码（驱动中linux,keycodes映射值）
    MPR121_EventType type;    // 事件类型
    struct timespec timestamp;// 新增：事件发生时间戳
} MPR121_Event;

// MPR121设备句柄
typedef struct {
    int fd;                   // 设备文件描述符
    char *dev_path;           // 设备路径（/dev/input/eventX）
    volatile int exit_flag;   // 退出标志（信号处理用）
} MPR121_DevHandle;


/**
 * @brief 初始化MPR121设备
 * @return 成功返回0，失败返回-1
 * @note 其他开发者首先调用此接口初始化设备
 */
 int mpr121_init(void);

 /**
 * @brief 读取MPR121触摸事件（阻塞/非阻塞兼容）
 * @param event 输出参数：存储读取到的事件
 * @param timeout_ms 超时时间（ms）：0=非阻塞，-1=永久阻塞，>0=超时阻塞
 * @return 成功返回0，超时返回1，失败返回-1
 * @note 其他开发者调用此接口读取事件
 */
int mpr121_read_event(MPR121_Event *event, int timeout_ms) ;

/**
 * @brief 释放MPR121设备资源
 * @return 成功返回0，失败返回-1
 * @note 其他开发者退出前调用此接口
 */
 int mpr121_deinit(void) ;

 /**
 * @brief 获取MPR121设备状态
 * @return 已初始化返回1，未初始化返回0
 * @note 辅助接口，用于检查设备状态
 */
int mpr121_is_inited(void);

/**
 * @brief 获取退出标志，在按下Ctrl+C时被设置
 * @return 0表示标志未被设置 ，1表示标志已设置，需要退出程序
 */
 int mpr121_get_exit_flag(void);
 
/**
 * @brief 获取MPR121设备文件描述符
 * @return 成功返回文件描述符，失败返回-1
 */
int mpr121_get_fd(void);
#endif
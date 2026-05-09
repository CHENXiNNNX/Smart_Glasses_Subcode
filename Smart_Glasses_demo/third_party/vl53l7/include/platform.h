/*********************************************************
 * platform.h  ——  VL53L7/L8 ULD Linux 平台适配层
 *
 * 适用：Rockchip RV1106（任何 Linux /dev/i2c-X 设备均可复用）
 * 作用：向 VL53LMZ_ULD_API 提供 I2C 读写 / 延时 / 字节交换 / 平台结构体
 *
 * 使用流程：
 *   VL53LMZ_Configuration dev = {0};
 *   vl53lmz_platform_open(&dev.platform, "/dev/i2c-3", 0x52);  // 0x52 = 8bit 写地址
 *   vl53lmz_init(&dev);
 *   ...
 *   vl53lmz_platform_close(&dev.platform);
 *********************************************************/
#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ULD API 要求的平台结构体。
 *        - address: 保留 ST 原定义（8-bit 格式，例如 0x52）。Linux 平台层内部
 *          会把它右移一位转换成 7-bit 给 ioctl(I2C_SLAVE/I2C_RDWR) 使用。
 *        - i2c_fd:  已打开的 /dev/i2c-X 文件描述符。
 */
typedef struct {
    uint16_t address;   /* ST 规范：VL53LMZ_DEFAULT_I2C_ADDRESS = 0x52（8bit） */
    int      i2c_fd;    /* Linux I2C 设备句柄 */
} VL53LMZ_Platform;

/* 每个 zone 允许保留的目标数（1~4）。数值越大 I2C 流量/内存占用越高。 */
#define VL53LMZ_NB_TARGET_PER_ZONE        1U

/* 是否使用 firmware 原始数据格式。启用后 ULD 不做单位换算，此时：
 *   distance_mm 的单位 = 1/4 mm（即实际 mm 需 /4）
 *   reflectance   单位 = 1/2 %
 *   range_sigma_mm 单位 = 1/128 mm
 *   ambient_per_spad 单位 = 1/2048 kcps/spad
 * 对于应用层直接展示，建议保持关闭，让 ULD 自动转成物理单位。
 * 如需追求最高精度（例如喂给算法），再打开此宏。
 */
/* #define VL53LMZ_USE_RAW_FORMAT */

/* 如需进一步裁剪 I2C/RAM，可按需启用以下宏屏蔽对应输出字段：
//#define VL53LMZ_DISABLE_AMBIENT_PER_SPAD
//#define VL53LMZ_DISABLE_NB_SPADS_ENABLED
//#define VL53LMZ_DISABLE_NB_TARGET_DETECTED
//#define VL53LMZ_DISABLE_SIGNAL_PER_SPAD
//#define VL53LMZ_DISABLE_RANGE_SIGMA_MM
//#define VL53LMZ_DISABLE_DISTANCE_MM
//#define VL53LMZ_DISABLE_REFLECTANCE_PERCENT
//#define VL53LMZ_DISABLE_TARGET_STATUS
//#define VL53LMZ_DISABLE_MOTION_INDICATOR
*/

/* ====== ULD API 必需的 6 个平台函数 ====== */
uint8_t RdByte(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress, uint8_t *p_value);
uint8_t WrByte(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress, uint8_t  value);
uint8_t RdMulti(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress, uint8_t *p_values, uint32_t size);
uint8_t WrMulti(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress, uint8_t *p_values, uint32_t size);
void    SwapBuffer(uint8_t *buffer, uint16_t size);
uint8_t WaitMs(VL53LMZ_Platform *p_platform, uint32_t TimeMs);

/* ====== 应用层便捷接口（Linux 专属） ====== */

/**
 * @brief 打开 I2C 总线，保存句柄与 8-bit 地址到 p_platform。
 * @param i2c_dev  例如 "/dev/i2c-3"
 * @param addr8bit 默认使用 0x52 即可（ULD 内部会校验）
 * @return 0 成功，其它失败（errno 由调用方自行检查）
 */
int  vl53lmz_platform_open(VL53LMZ_Platform *p_platform,
                           const char *i2c_dev,
                           uint16_t addr8bit);

/** @brief 关闭 I2C 句柄，内部会置 i2c_fd = -1。 */
void vl53lmz_platform_close(VL53LMZ_Platform *p_platform);

#ifdef __cplusplus
}
#endif

#endif /* _PLATFORM_H_ */

/*********************************************************
 * platform.c  ——  VL53L7/L8 ULD Linux 平台适配实现
 *
 * 核心事实：
 *   1. VL53L7 寄存器地址 16-bit（需要先写 2 字节 [hi,lo]，再读/写数据）。
 *   2. ULD 内部以 8-bit 形式保存 I2C 地址（如 0x52），Linux ioctl(I2C_RDWR)
 *      使用 7-bit（0x29），平台层在 addr7 = addr >> 1 处统一转换。
 *   3. 初始化阶段会调用 WrMulti 一次性下发 0x8000 字节 firmware。为适配 RK
 *      I2C 控制器，一律按 CHUNK_SIZE(4096) 分块发送。读取同理分块。
 *   4. 失败返回值遵循 ULD 约定：非 0 即错误。
 *********************************************************/

#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* 单次 I2C 传输的数据分块大小（不含 2 字节寄存器地址头）。
 * 4096 是在 RK I2C 控制器与 Linux i2c-core 默认限制下最稳的折中值。 */
#ifndef VL53LMZ_I2C_CHUNK_SIZE
#define VL53LMZ_I2C_CHUNK_SIZE   (4096U)
#endif

/* ---------- 内部工具：I2C 读/写（单块） ---------- */

static uint8_t i2c_write_chunk(VL53LMZ_Platform *p_platform,
                               uint16_t reg,
                               const uint8_t *data,
                               uint32_t size)
{
    if (!p_platform || p_platform->i2c_fd < 0) return 1;

    /* 拼包：2 字节寄存器（大端） + 数据 */
    uint32_t total = size + 2U;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return 2;
    buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    buf[1] = (uint8_t)(reg & 0xFF);
    if (size) memcpy(buf + 2, data, size);

    struct i2c_msg msg;
    msg.addr  = (uint16_t)(p_platform->address >> 1);  /* 8bit -> 7bit */
    msg.flags = 0;                                     /* 写 */
    msg.len   = (uint16_t)total;
    msg.buf   = buf;

    struct i2c_rdwr_ioctl_data pkt = { &msg, 1 };
    int ret = ioctl(p_platform->i2c_fd, I2C_RDWR, &pkt);
    free(buf);
    return (ret < 0) ? 3U : 0U;
}

static uint8_t i2c_read_chunk(VL53LMZ_Platform *p_platform,
                              uint16_t reg,
                              uint8_t *data,
                              uint32_t size)
{
    if (!p_platform || p_platform->i2c_fd < 0) return 1;

    uint8_t reg_buf[2];
    reg_buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    reg_buf[1] = (uint8_t)(reg & 0xFF);

    struct i2c_msg msgs[2];
    msgs[0].addr  = (uint16_t)(p_platform->address >> 1);
    msgs[0].flags = 0;
    msgs[0].len   = 2;
    msgs[0].buf   = reg_buf;

    msgs[1].addr  = (uint16_t)(p_platform->address >> 1);
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = (uint16_t)size;
    msgs[1].buf   = data;

    struct i2c_rdwr_ioctl_data pkt = { msgs, 2 };
    int ret = ioctl(p_platform->i2c_fd, I2C_RDWR, &pkt);
    return (ret < 0) ? 3U : 0U;
}

/* ---------- ULD API 必需函数实现 ---------- */

uint8_t RdByte(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress, uint8_t *p_value)
{
    return i2c_read_chunk(p_platform, RegisterAdress, p_value, 1);
}

uint8_t WrByte(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress, uint8_t value)
{
    return i2c_write_chunk(p_platform, RegisterAdress, &value, 1);
}

uint8_t RdMulti(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress,
                uint8_t *p_values, uint32_t size)
{
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > VL53LMZ_I2C_CHUNK_SIZE) chunk = VL53LMZ_I2C_CHUNK_SIZE;
        uint8_t st = i2c_read_chunk(p_platform,
                                    (uint16_t)(RegisterAdress + offset),
                                    p_values + offset, chunk);
        if (st) return st;
        offset += chunk;
    }
    return 0;
}

uint8_t WrMulti(VL53LMZ_Platform *p_platform, uint16_t RegisterAdress,
                uint8_t *p_values, uint32_t size)
{
    /* 固件下载阶段会一次传 32KB，需要分块；注意 RegisterAdress 要随 offset 累加。 */
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > VL53LMZ_I2C_CHUNK_SIZE) chunk = VL53LMZ_I2C_CHUNK_SIZE;
        uint8_t st = i2c_write_chunk(p_platform,
                                     (uint16_t)(RegisterAdress + offset),
                                     p_values + offset, chunk);
        if (st) return st;
        offset += chunk;
    }
    return 0;
}

void SwapBuffer(uint8_t *buffer, uint16_t size)
{
    uint32_t i, tmp;
    for (i = 0; i < size; i += 4) {
        tmp = ((uint32_t)buffer[i]     << 24) |
              ((uint32_t)buffer[i + 1] << 16) |
              ((uint32_t)buffer[i + 2] << 8)  |
              ((uint32_t)buffer[i + 3]);
        memcpy(&buffer[i], &tmp, 4);
    }
}

uint8_t WaitMs(VL53LMZ_Platform *p_platform, uint32_t TimeMs)
{
    (void)p_platform;
    struct timespec ts;
    ts.tv_sec  = TimeMs / 1000U;
    ts.tv_nsec = (long)(TimeMs % 1000U) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* 被信号打断时继续睡剩余时间 */
    }
    return 0;
}

/* ---------- 应用层辅助接口 ---------- */

int vl53lmz_platform_open(VL53LMZ_Platform *p_platform,
                          const char *i2c_dev,
                          uint16_t addr8bit)
{
    if (!p_platform || !i2c_dev) return -EINVAL;
    p_platform->i2c_fd  = -1;
    p_platform->address = addr8bit;

    int fd = open(i2c_dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[vl53l7] open %s failed: %s\n", i2c_dev, strerror(errno));
        return -errno;
    }

    /* 先尝试设置 slave 地址（使用 7-bit），便于 read/write 风格访问；
     * I2C_RDWR 路径本身也没问题，这里只是做一次健康检查。 */
    uint16_t addr7 = (uint16_t)(addr8bit >> 1);
    if (ioctl(fd, I2C_SLAVE, addr7) < 0) {
        fprintf(stderr, "[vl53l7] I2C_SLAVE 0x%02x failed: %s\n",
                addr7, strerror(errno));
        close(fd);
        return -errno;
    }

    p_platform->i2c_fd = fd;
    return 0;
}

void vl53lmz_platform_close(VL53LMZ_Platform *p_platform)
{
    if (p_platform && p_platform->i2c_fd >= 0) {
        close(p_platform->i2c_fd);
        p_platform->i2c_fd = -1;
    }
}

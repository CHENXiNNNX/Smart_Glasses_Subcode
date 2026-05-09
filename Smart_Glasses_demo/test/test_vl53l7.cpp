/**
 * test_vl53l7.cpp —— VL53L7/L8 最小化 Linux 测距测试
 *
 * 运行：
 *   ./test_vl53l7 [/dev/i2c-3] [0x52]
 *
 * 行为：
 *   1. 打开 I2C 总线并保活 VL53L7；
 *   2. 下载固件并初始化（约 0.5~1s）；
 *   3. 设置 8x8 分辨率 + 15Hz 连续测距；
 *   4. 采集 20 帧，逐帧打印 64 个 zone 的距离与状态；
 *   5. 停止测距并释放资源。
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <ctime>

extern "C" {
#include "platform.h"
#include "vl53lmz_api.h"
}

static volatile int g_running = 1;
static void on_sigint(int) { g_running = 0; }

/* ULD 约定：target_status == 5 或 9 表示测距有效。
 *   5 : Range valid
 *   9 : Range valid with large pulse (lower confidence but usable)
 * 其它值都视为该 zone 无有效目标。 */
static inline bool is_valid_status(uint8_t s)
{
    return (s == 5) || (s == 9);
}

static inline double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    const char *i2c_dev = (argc > 1) ? argv[1] : "/dev/i2c-3";
    uint16_t    addr8   = (argc > 2) ? (uint16_t)strtol(argv[2], nullptr, 0)
                                     : VL53LMZ_DEFAULT_I2C_ADDRESS; /* 0x52 */

    signal(SIGINT, on_sigint);

    VL53LMZ_Configuration dev;
    memset(&dev, 0, sizeof(dev));

    /* 1) 打开 I2C，配置平台结构体 */
    if (vl53lmz_platform_open(&dev.platform, i2c_dev, addr8) != 0) {
        fprintf(stderr, "open %s @0x%02x failed\n", i2c_dev, addr8);
        return 1;
    }
    printf("[vl53l7] i2c=%s, addr8=0x%02x, addr7=0x%02x\n",
           i2c_dev, addr8, addr8 >> 1);

    /* 2) Alive 检查 + 固件下载 */
    uint8_t isAlive = 0;
    if (vl53lmz_is_alive(&dev, &isAlive) || !isAlive) {
        fprintf(stderr, "[vl53l7] sensor not detected\n");
        vl53lmz_platform_close(&dev.platform);
        return 2;
    }
    printf("[vl53l7] sensor alive, loading firmware...\n");

    if (vl53lmz_init(&dev) != VL53LMZ_STATUS_OK) {
        fprintf(stderr, "[vl53l7] init failed\n");
        vl53lmz_platform_close(&dev.platform);
        return 3;
    }
    printf("[vl53l7] ULD ready, version = %s\n", VL53LMZ_API_REVISION);

    /* 3) 配置：8x8 + 最近目标优先 + 15Hz 连续 */
    vl53lmz_set_resolution(&dev, VL53LMZ_RESOLUTION_8X8);
    vl53lmz_set_target_order(&dev, VL53LMZ_TARGET_ORDER_CLOSEST);
    vl53lmz_set_ranging_frequency_hz(&dev, 15);
    vl53lmz_set_ranging_mode(&dev, VL53LMZ_RANGING_MODE_CONTINUOUS);

    if (vl53lmz_start_ranging(&dev) != VL53LMZ_STATUS_OK) {
        fprintf(stderr, "[vl53l7] start_ranging failed\n");
        vl53lmz_platform_close(&dev.platform);
        return 4;
    }

    /* 4) 循环检测，直到收到 Ctrl+C (SIGINT)
     *    用 ANSI 控制序列原地刷新 8x8 矩阵，便于观察深度变化。
     *    - "\x1b[H"  光标移到左上 (1,1)
     *    - "\x1b[2J" 清屏
     *    - "\x1b[?25l" 隐藏光标；"\x1b[?25h" 恢复光标 */
    VL53LMZ_ResultsData results;
    uint32_t frames   = 0;
    double   t_start  = now_sec();
    double   t_prev   = t_start;

    printf("\x1b[2J\x1b[?25l");
    fflush(stdout);

    printf("[vl53l7] running... press Ctrl+C to quit\n");

    while (g_running) {
        uint8_t ready = 0;
        vl53lmz_check_data_ready(&dev, &ready);
        if (!ready) {
            WaitMs(&dev.platform, 5);
            continue;
        }
        if (vl53lmz_get_ranging_data(&dev, &results) != VL53LMZ_STATUS_OK) {
            WaitMs(&dev.platform, 5);
            continue;
        }

        double t_now = now_sec();
        double dt    = t_now - t_prev;
        t_prev       = t_now;
        ++frames;

        int valid_cnt = 0;

        printf("\x1b[H");                                  /* 光标回到左上 */
        printf("\x1b[2K"                                    /* 擦除当前行 */
               "VL53L7/L8 live  frame=%-8u stream=%-4u "
               "fps=%5.1f  dT=%5.1fms  T=%3dC  "
               "(Ctrl+C 退出)\n",
               frames, dev.streamcount,
               dt > 0.0 ? 1.0 / dt : 0.0,
               dt * 1000.0,
               (int)results.silicon_temp_degc);

        for (int row = 0; row < 8; ++row) {
            printf("\x1b[2K");                              /* 擦除当前行 */
            for (int col = 0; col < 8; ++col) {
                int i   = row * 8 + col;
                int idx = VL53LMZ_NB_TARGET_PER_ZONE * i;
                uint8_t st  = results.target_status[idx];
                int16_t dmm = results.distance_mm[idx];
                if (is_valid_status(st)) {
                    printf("%5d ", (int)dmm);               /* 单位 mm */
                    ++valid_cnt;
                } else {
                    printf("    . ");
                }
            }
            printf("\n");
        }
        printf("\x1b[2Kvalid zones: %2d / 64   avg fps: %5.2f   "
               "elapsed: %7.1fs\n",
               valid_cnt,
               frames / (now_sec() - t_start),
               now_sec() - t_start);
        fflush(stdout);
    }

    /* 恢复光标、换几行避免最后一行被覆盖 */
    printf("\x1b[?25h\n\n");

    /* 5) 清理 */
    vl53lmz_stop_ranging(&dev);
    vl53lmz_platform_close(&dev.platform);
    printf("[vl53l7] done.\n");
    return 0;
}

# ============================================================
# VL53L7/L8 ULD 驱动（Linux / RV1106 适配版）
# 产出：静态库 vl53l7，供 main 或测试可执行文件链接
# ============================================================

set(VL53L7_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(VL53L7_ULD_SRC
    ${VL53L7_ROOT}/VL53LMZ_ULD_API/src/vl53lmz_api.c
    ${VL53L7_ROOT}/VL53LMZ_ULD_API/src/vl53lmz_plugin_cnh.c
    ${VL53L7_ROOT}/VL53LMZ_ULD_API/src/vl53lmz_plugin_detection_thresholds.c
    ${VL53L7_ROOT}/VL53LMZ_ULD_API/src/vl53lmz_plugin_motion_indicator.c
    ${VL53L7_ROOT}/VL53LMZ_ULD_API/src/vl53lmz_plugin_xtalk.c
)

set(VL53L7_PLATFORM_SRC
    ${VL53L7_ROOT}/src/platform.c
)

add_library(vl53l7 STATIC
    ${VL53L7_ULD_SRC}
    ${VL53L7_PLATFORM_SRC}
)

# ULD 源码里是 #include "platform.h"，必须把自研 platform 目录优先
target_include_directories(vl53l7 PUBLIC
    ${VL53L7_ROOT}/include                  # Linux 版 platform.h
    ${VL53L7_ROOT}/VL53LMZ_ULD_API/inc      # ST ULD 公共头
)

# ULD 源码为 C99，并带有未使用参数等非致命告警；统一放宽
target_compile_options(vl53l7 PRIVATE
    -Wall
    -Wno-unused-parameter
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-sign-compare
)

set_target_properties(vl53l7 PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

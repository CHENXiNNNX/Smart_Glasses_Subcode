# Specify the cross-compilation toolchain
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(SDK_PATH "/home/irex/WorkSpace/Smart_Glasses/SDK/rv1106-sdk")

# Specify the compiler paths
set(CMAKE_C_COMPILER ${SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-g++)

# Specify the sysroot (if available)
set(CMAKE_SYSROOT ${SDK_PATH}/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot)

# Add paths to find libraries and includes
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

# 添加Speex库和头文件路径
set(SPEEX_INCLUDE_DIRS ${SDK_PATH}/sysdrv/source/buildroot/buildroot-2023.02.6/output/build/speexdsp-1.2.1/include)
set(SPEEX_LIBRARY_DIRS ${SDK_PATH}/sysdrv/out/rootfs_uclibc_rv1106/usr/lib)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
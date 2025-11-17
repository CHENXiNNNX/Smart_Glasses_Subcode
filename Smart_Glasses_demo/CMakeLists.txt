cmake_minimum_required(VERSION 3.10)

# 设置ARM交叉编译工具链
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake" CACHE FILEPATH "Default toolchain file for ARM builds")
    message(STATUS "Using ARM toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
endif()

message(STATUS "Building for ARM architecture - Smart Glasses")

# 工程名称
project(SmartGlasses)

# 设置交叉编译链的pkg-config路径和环境变量
set(SDK_PATH "/home/irex/WorkSpace/Smart_Glasses/SDK/rv1106-sdk")
set(CROSS_PKG_CONFIG "${SDK_PATH}/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/bin/pkg-config")

# 检查交叉编译链的pkg-config是否存在
if(EXISTS ${CROSS_PKG_CONFIG})
    set(PKG_CONFIG_EXECUTABLE ${CROSS_PKG_CONFIG} CACHE FILEPATH "Path to cross-compilation pkg-config")
    message(STATUS "Using cross-compilation pkg-config: ${CROSS_PKG_CONFIG}")
else()
    message(WARNING "Cross-compilation pkg-config not found at ${CROSS_PKG_CONFIG}, using system pkg-config")
endif()

# 设置pkg-config环境变量
if(CMAKE_SYSROOT)
    set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/pkgconfig")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
    set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig")
    message(STATUS "PKG_CONFIG_PATH set to: ${CMAKE_SYSROOT}/usr/lib/pkgconfig")
    message(STATUS "PKG_CONFIG_SYSROOT_DIR set to: ${CMAKE_SYSROOT}")
else()
    message(WARNING "CMAKE_SYSROOT is not set! PKG_CONFIG_PATH may not work correctly.")
endif()

# Options
option(BUILD_SHARED_LIBS "Build shared library" OFF) # 默认构建静态库
option(BUILD_SHARED_DEPS_LIBS "Build submodules as shared libraries" OFF)
option(USE_GNUTLS "Use GnuTLS instead of OpenSSL" OFF)
option(USE_MBEDTLS "Use Mbed TLS instead of OpenSSL" OFF)
option(USE_NICE "Use libnice instead of libjuice" OFF)
option(PREFER_SYSTEM_LIB "Prefer system libraries over submodules" OFF)
option(USE_SYSTEM_SRTP "Use system libSRTP" ${PREFER_SYSTEM_LIB})
option(USE_SYSTEM_JUICE "Use system libjuice" ${PREFER_SYSTEM_LIB})
option(USE_SYSTEM_USRSCTP "Use system libusrsctp" ${PREFER_SYSTEM_LIB})
option(USE_SYSTEM_PLOG "Use system Plog" ${PREFER_SYSTEM_LIB})
option(USE_SYSTEM_JSON "Use system Nlohmann JSON" ${PREFER_SYSTEM_LIB})
option(NO_WEBSOCKET "Disable WebSocket support" OFF)
option(NO_MEDIA "Disable media transport support" OFF)
option(NO_EXAMPLES "Disable examples" ON)
option(NO_TESTS "Disable tests build" ON)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(CAPI_STDCALL "Set calling convention of C API callbacks stdcall" OFF)
option(SCTP_DEBUG "Enable SCTP debugging output to verbose log" OFF)
option(RTC_UPDATE_VERSION_HEADER "Enable updating the version header" OFF)

# 设置C/C++标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出路径
set(OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 添加子目录
add_subdirectory(utils)          # 公共工具模块
add_subdirectory(common)         # 通用组件模块
add_subdirectory(app)            # 应用核心模块
add_subdirectory(third_party)    # 第三方库模块
add_subdirectory(third_party/libdatachannel) # WebRTC网络通信库

# 定义主程序源文件
set(MAIN_SOURCES main.cpp)

# 创建可执行文件
add_executable(main ${MAIN_SOURCES})

# 链接核心库
target_link_libraries(
    main 
    utils
    common
    app
    datachannel-static  # WebRTC网络通信库
)

# 复制配置文件到输出目录
add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/utils/system_para.conf
            ${OUTPUT_PATH}/system_para.conf
    COMMENT "Copying system_para.conf to bin directory"
)

add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/utils/gaode_adcode.json
            ${OUTPUT_PATH}/gaode_adcode.json
    COMMENT "Copying gaode_adcode.json to bin directory"
)

add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/utils/cacert.pem
            ${OUTPUT_PATH}/cacert.pem
    COMMENT "Copying cacert.pem to bin directory"
)

# 引用AIChat_demo中的库 - 已迁移到内部模块，暂时注释
# set(AI_CHAT_CLIENT_PATH "${PROJECT_SOURCE_DIR}/../AIChat_demo/Client" CACHE PATH "Path to AIChat_demo Client")
# add_subdirectory(${AI_CHAT_CLIENT_PATH} ${CMAKE_BINARY_DIR}/AIChat_build)
# target_link_libraries(main AIchat-c-interface AIChatCore)

# Ensure pkg-config is available
find_package(PkgConfig REQUIRED)

# Find required packages
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(WEBSOCKETPP REQUIRED)

# Link libraries for opus and websocketpp
target_link_libraries(main
    ${OPUS_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${JSONCPP_LIBRARIES} 
    ${SAMPLERATE_LIBRARIES}
    ${WEBSOCKETPP_LIBRARIES}
    ${CURL_LIBRARIES}
    snowboy-detect-c-wrapper  
)

# 包含Snowboy头文件路径
target_include_directories(main PRIVATE ${SNOWBOY_INCLUDE_DIRS})

# 复制AIChat所需文件
add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources/models
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/audio
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/snowboy/resources/common.res
            ${OUTPUT_PATH}/third_party/snowboy/resources/common.res
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/snowboy/resources/models/xiaozhi.pmdl
            ${OUTPUT_PATH}/third_party/snowboy/resources/models/xiaozhi.pmdl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/audio/waked.pcm
            ${OUTPUT_PATH}/third_party/audio/waked.pcm
    COMMENT "Copying AI Chat necessary resource files to executable output directory"
)

# opencv配置
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv_rv1106_410/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
target_link_libraries(main ${OpenCV_LIBS})

# jpeg_turbo配置
target_include_directories(main PRIVATE ${JPEG_TURBO_INCLUDES})
target_link_libraries(main ${LIBTURBOJPEG})

# rknn runtime配置
target_include_directories(main PRIVATE ${LIBRKNNRT_INCLUDES})
target_link_libraries(main ${LIBRKNNRT})

# 设置SDK中的rkmpi和rkaiq路径
set(SDK_MEDIA_PATH ${SDK_PATH}/media)
set(ROCKIT_ROOT ${SDK_MEDIA_PATH}/rockit/out)
set(RKAIQ_ROOT ${SDK_MEDIA_PATH}/isp/out)
set(RGA_ROOT ${SDK_MEDIA_PATH}/rga/release_rga_rv1106_arm-rockchip830-linux-uclibcgnueabihf)
set(MPP_ROOT ${SDK_MEDIA_PATH}/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf)
set(IVA_ROOT ${SDK_MEDIA_PATH}/iva/out)
set(SAMPLES_ROOT ${SDK_MEDIA_PATH}/samples/example/common)
set(SAMPLE_COMM_LIB ${SAMPLES_ROOT}/lib)

# Rockit 库
set(ROCKIT_INC ${ROCKIT_ROOT}/include)
set(ROCKIT_LIB ${ROCKIT_ROOT}/lib)

# RKAIQ 库
set(RKAIQ_INC ${RKAIQ_ROOT}/include)
set(RKAIQ_LIB ${RKAIQ_ROOT}/lib)

# RGA 库
set(RGA_INC ${RGA_ROOT}/include)
set(RGA_LIB ${RGA_ROOT}/lib)

# MPP 库
set(MPP_INC ${MPP_ROOT}/include)
set(MPP_LIB ${MPP_ROOT}/lib)

# IVA 库
set(IVA_INC ${IVA_ROOT}/include)
set(IVA_LIB ${IVA_ROOT}/lib)

target_link_directories(main PRIVATE 
    ${ROCKIT_LIB}
    ${RKAIQ_LIB}
    ${RGA_LIB}              # RGA库（rockit库依赖）
    ${MPP_LIB}              # MPP库
    ${IVA_LIB}              # IVA库
    ${SAMPLE_COMM_LIB}      # Sample comm库
)

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${ROCKIT_LIB}:${RKAIQ_LIB}:${RGA_LIB}:${MPP_LIB}:${IVA_LIB}:${SAMPLE_COMM_LIB}:${CMAKE_SYSROOT}/usr/lib"
)

target_include_directories(main PRIVATE
    # Rockit MPI 头文件
    ${ROCKIT_INC}
    # RKAIQ 头文件
    ${RKAIQ_INC}
    ${RKAIQ_INC}/rkaiq/uAPI2
    ${RKAIQ_INC}/rkaiq/common
    ${RKAIQ_INC}/rkaiq/xcore
    ${RKAIQ_INC}/rkaiq/algos
    ${RKAIQ_INC}/rkaiq/iq_parser
    ${RKAIQ_INC}/rkaiq/iq_parser_v2
    # RGA 头文件
    ${RGA_INC}
    # MPP 头文件
    ${MPP_INC}
    # IVA 头文件
    ${IVA_INC}
    # Sample comm 头文件
    ${SAMPLES_ROOT}
    ${SAMPLES_ROOT}/isp3.x      # Sample comm ISP 3.x 头文件
)

target_link_libraries(main
    rockit_full          
    rkaiq                 
    rga                   
    rockchip_mpp         
    rockiva               
    sample_comm          
    pthread
    dl
    m
)

# ================== JSON 库配置 ==================
# 使用 libdatachannel 自带的 json 库
target_include_directories(main PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdatachannel/deps/json/single_include
)

# ================== libdatachannel WebRTC 配置 ==================
# 配置 libdatachannel 编译选项
set_target_properties(datachannel-static PROPERTIES
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET default
)   

# 为 libdatachannel 设置编译定义
target_compile_definitions(datachannel-static PRIVATE
    RTC_STATIC=1          # 静态链接模式
    RTC_ENABLE_WEBSOCKET=1 # 启用WebSocket支持
    RTC_ENABLE_MEDIA=1     # 启用媒体功能（音视频）
    USE_GNUTLS=0          # 禁用GnuTLS加密库
    USE_MBEDTLS=0         # 禁用mbedTLS加密库
    USE_NICE=0            # 禁用libnice ICE库
    RTC_SYSTEM_JUICE=0    # 使用内置的juice ICE实现
    RTC_SYSTEM_SRTP=0     # 使用内置的SRTP实现
    RTC_SYSTEM_USRSCTP=0  # 使用内置的SCTP实现
    RTC_SYSTEM_PLOG=0     # 使用内置的日志库
    RTC_SYSTEM_JSON=0     # 使用内置的JSON库
)

# 确保 libdatachannel 使用与主项目相同的工具链
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    set_target_properties(datachannel-static PROPERTIES
        CMAKE_TOOLCHAIN_FILE ${CMAKE_TOOLCHAIN_FILE}
    )
endif()

# 设置可执行文件输出目录
set_target_properties(main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_PATH}"
)

# 添加清理目标
if(NOT TARGET clean-all)
    add_custom_target(clean-all
        COMMAND find "${CMAKE_BINARY_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        COMMENT "Cleaning all generated files."
    )
endif()
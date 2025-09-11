cmake_minimum_required(VERSION 3.10)

# 设置ARM交叉编译工具链
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake" CACHE FILEPATH "Default toolchain file for ARM builds")
    message(STATUS "Using ARM toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
endif()
# 设置交叉编译环境下 pkg-config 使用的路径
set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/pkgconfig")
message(STATUS "Building for ARM architecture - Smart Glasses")

# 工程名称
project(SmartGlasses)

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
add_subdirectory(3rdparty)       # 第三方库模块
add_subdirectory(libdatachannel) # WebRTC网络通信库

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
find_package(WEBSOCKETPP REQUIRED)

# Link libraries for opus and websocketpp
target_link_libraries(main
    ${OPUS_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${WEBSOCKETPP_LIBRARIES}
    snowboy-detect-c-wrapper
)

# 复制AIChat所需文件
add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources/models
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/audio
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/snowboy/resources/common.res
            ${OUTPUT_PATH}/third_party/snowboy/resources/common.res
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/snowboy/resources/models/echo.pmdl
            ${OUTPUT_PATH}/third_party/snowboy/resources/models/echo.pmdl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/audio/waked.pcm
            ${OUTPUT_PATH}/third_party/audio/waked.pcm
    COMMENT "Copying AI Chat necessary resource files to executable output directory"
)

# opencv配置
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/opencv_rv1106_410/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
target_link_libraries(main ${OpenCV_LIBS})

# jpeg_turbo配置
target_include_directories(main PRIVATE ${JPEG_TURBO_INCLUDES})
target_link_libraries(main ${LIBTURBOJPEG})

# rknn runtime配置
target_include_directories(main PRIVATE ${LIBRKNNRT_INCLUDES})
target_link_libraries(main ${LIBRKNNRT})

# rga配置
target_include_directories(main PRIVATE ${LIBRGA_INCLUDES})
target_link_libraries(main ${LIBRGA})

# 设置rkmpi路径
set(RKMPI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/rkmpi)
set(RKMPI_INC  ${RKMPI_ROOT}/include)
set(RKMPI_LIB  ${RKMPI_ROOT}/lib)

target_link_directories(main PRIVATE ${RKMPI_LIB})

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${RKMPI_LIB}:${CMAKE_SYSROOT}/usr/lib"
)

target_include_directories(main PRIVATE
    ${RKMPI_INC}
    ${RKMPI_INC}/rknn
    ${RKMPI_INC}/rkaiq
    ${RKMPI_INC}/rkaiq/uAPI2
    ${RKMPI_INC}/rkaiq/common
    ${RKMPI_INC}/rkaiq/xcore
    ${RKMPI_INC}/rkaiq/algos
    ${RKMPI_INC}/rkaiq/iq_parser
    ${RKMPI_INC}/rkaiq/iq_parser_v2
    ${RKMPI_INC}/rkaiq/smartIr
)

target_link_libraries(main
    rockiva
    sample_comm
    rockit_full
    rockchip_mpp
    rkaiq
    rtsp
    pthread
    dl
    m
)

# ================== JSON 库配置 ==================
# 使用 libdatachannel 自带的 json 库
target_include_directories(main PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/libdatachannel/deps/json/single_include
)

# ================== libdatachannel WebRTC 配置 ==================
# 配置 libdatachannel 编译选项
set_target_properties(datachannel-static PROPERTIES
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET default
)

# 为 libdatachannel 设置编译定义
target_compile_definitions(datachannel-static PRIVATE
    RTC_STATIC=1
    RTC_ENABLE_WEBSOCKET=1
    RTC_ENABLE_MEDIA=1
    USE_GNUTLS=0
    USE_MBEDTLS=0
    USE_NICE=0
    RTC_SYSTEM_JUICE=0
    RTC_SYSTEM_SRTP=0
    RTC_SYSTEM_USRSCTP=0
    RTC_SYSTEM_PLOG=0
    RTC_SYSTEM_JSON=0
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
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
option(NO_EXAMPLES "Disable examples" OFF)
option(NO_TESTS "Disable tests build" OFF)
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

# 引用AIChat_demo中的库
set(AI_CHAT_CLIENT_PATH "${PROJECT_SOURCE_DIR}/../AIChat_demo/Client" CACHE PATH "Path to AIChat_demo Client")
add_subdirectory(${AI_CHAT_CLIENT_PATH} ${CMAKE_BINARY_DIR}/AIChat_build)
target_link_libraries(main AIchat-c-interface AIChatCore)

# 复制AIChat所需文件
add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources/models
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/audio
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AI_CHAT_CLIENT_PATH}/third_party/snowboy/resources/common.res
            ${OUTPUT_PATH}/third_party/snowboy/resources/common.res
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AI_CHAT_CLIENT_PATH}/third_party/snowboy/resources/models/echo.pmdl
            ${OUTPUT_PATH}/third_party/snowboy/resources/models/echo.pmdl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AI_CHAT_CLIENT_PATH}/third_party/audio/waked.pcm
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

# ================== libdatachannel WebRTC 配置 ==================
# libdatachannel 提供了以下功能：
# - WebRTC Data Channels: 实时数据传输通道
# - WebRTC Media Transport: 音视频媒体传输
# - WebSocket: 信令通信支持
# - 多种加密后端: OpenSSL/GnuTLS/Mbed TLS
# - ICE 连接建立: libjuice/libnice 支持

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

# # 运行时库目录与 rpath
# add_custom_command(
#     TARGET main POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/lib
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#             ${LIBRKNNRT}
#             ${OUTPUT_PATH}/lib/
#     COMMENT "Copying RKNN runtime (.so) to bin/lib"
# )

# # 让可执行在 $ORIGIN/lib 下查找 .so
# set_target_properties(main PROPERTIES
#     BUILD_RPATH "\$ORIGIN/lib"
#     INSTALL_RPATH "\$ORIGIN/lib"
# )

# # 配置rkmpi头文件
# target_include_directories(main PRIVATE ${RKMPI_INC})

# # 配置rkmpi链接库
# target_link_libraries(main
#     ${RKMPI_LIB}/librockit_full.a
#     ${RKMPI_LIB}/librockchip_mpp.a
#     ${RKMPI_LIB}/librkaiq.a
#     # ${RKMPI_LIB}/librga.so     # 若未用 ${LIBRGA}，再开启这一行
#     ${RKMPI_LIB}/librockiva.a
#     ${RKMPI_LIB}/librtsp.a
#     ${RKMPI_LIB}/libsample_comm.a
#     pthread dl m
# )

# 复制运行时 .so 至 bin/lib
# add_custom_command(
#     TARGET main POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/lib
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#             ${RKMPI_LIB}/librockit_full.so
#             ${RKMPI_LIB}/librockchip_mpp.so
#             ${RKMPI_LIB}/librkaiq.so
#             # ${RKMPI_LIB}/librga.so         # 若链接 .so 再拷贝
#             ${RKMPI_LIB}/librockiva.so
#             ${OUTPUT_PATH}/lib/
#     COMMENT "Copying RKMPI shared libs to bin/lib"
# )

# ARM平台特定配置 - AI相机和YOLO模型
# if(TARGET_ARM)
#     # YOLOv5 AI相机模块
#     set(AI_CAMERA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../yolov5_demo" CACHE PATH "Path to YOLOv5 demo")
#     add_subdirectory(${AI_CAMERA_PATH}/cpp ${CMAKE_BINARY_DIR}/AICamera_build)
#     target_link_libraries(main yoloCameraCore)
    
#     # 复制YOLO模型文件
#     add_custom_command(
#         TARGET main POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/model # 确保目标目录存在
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                 ${AI_CAMERA_PATH}/model/bus.jpg
#                 ${OUTPUT_PATH}/model/bus.jpg
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                 ${AI_CAMERA_PATH}/model/coco_80_labels_list.txt
#                 ${OUTPUT_PATH}/model/coco_80_labels_list.txt
#         COMMENT "Copying YOLOv5 model files to executable output directory"
#     )
    
#     # 复制所有RKNN模型文件
#     file(GLOB RKNN_FILES "${AI_CAMERA_PATH}/model/*.rknn")
#     foreach(RKNN_FILE ${RKNN_FILES})
#         get_filename_component(FILE_NAME ${RKNN_FILE} NAME) # 获取文件名
#         add_custom_command(
#             TARGET main POST_BUILD
#             COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                     ${RKNN_FILE}
#                     ${OUTPUT_PATH}/model/${FILE_NAME}
#             COMMENT "Copying ${FILE_NAME} to executable output directory"
#         )
#     endforeach()

#     # 复制ARM平台共享库
#     set(SHARED_LIBS
#         "${AI_CAMERA_PATH}/cpp/3rdparty/librga/Linux/armhf_uclibc/librga.so"
#         "${AI_CAMERA_PATH}/cpp/3rdparty/rknpu2/Linux/armhf-uclibc/librknnmrt.so"
#     )

#     # 创建lib目录
#     add_custom_command(
#         TARGET main POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/lib
#         COMMENT "Creating lib directory for shared libraries"
#     )

#     # 复制共享库文件
#     foreach(LIB_FILE ${SHARED_LIBS})
#         get_filename_component(FILE_NAME ${LIB_FILE} NAME) # 获取文件名
#         add_custom_command(
#             TARGET main POST_BUILD
#             COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                     ${LIB_FILE}
#                     ${OUTPUT_PATH}/lib/${FILE_NAME}
#             COMMENT "Copying ${FILE_NAME} to executable output directory"
#         )
#     endforeach()
#     # 设置运行时 rpath，以便可执行文件能够在运行时找到指定目录下的共享库
#     set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,\$ORIGIN/lib")
# endif()

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
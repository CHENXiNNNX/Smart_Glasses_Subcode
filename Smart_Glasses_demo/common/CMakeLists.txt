file(GLOB_RECURSE COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"  # 查找当前目录及子目录中的所有 .c 文件
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"  # 查找当前目录及子目录中的所有 .cc 文件
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"  # 查找当前目录及子目录中的所有 .cpp 文件
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"  # 查找当前目录及子目录中的所有 .h 文件
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"  # 查找当前目录及子目录中的所有 .hpp 文件
)

# 定义 common 模块为一个静态库
add_library(common STATIC ${COMMON_SOURCES})

# opencv 依赖
set(OpenCV_DIR "${PROJECT_SOURCE_DIR}/opencv_rv1106_410/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
target_link_libraries(common ${OpenCV_LIBS})

# 设置头文件包含目录
target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # libdatachannel headers
    ${PROJECT_SOURCE_DIR}/libdatachannel/include
    ${PROJECT_SOURCE_DIR}/libdatachannel/deps/json/single_include
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/rockit/out/include
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq/uAPI2
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq/common
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq/xcore
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq/algos
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq/iq_parser
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/isp/out/include/rkaiq/iq_parser_v2
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/samples/example/common
    ${PROJECT_SOURCE_DIR}/../../SDK/rv1106-sdk/media/samples/example/common/isp3.x

)

# 编译选项 
target_compile_options(common PRIVATE
    -Wall
    -Wextra
)

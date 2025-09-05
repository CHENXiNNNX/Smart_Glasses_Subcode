# App module CMakeLists.txt

# 收集当前目录及子目录中的所有 .c 文件
file(GLOB_RECURSE APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

# 创建 app 静态库
add_library(app STATIC ${APP_SOURCES})

# opencv 依赖
set(OpenCV_DIR "${PROJECT_SOURCE_DIR}/opencv_rv1106_410/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
target_link_libraries(app ${OpenCV_LIBS})

# 设置头文件包含目录
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot
    ${CMAKE_CURRENT_SOURCE_DIR}/media
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/test
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/rtsp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/websocket
    # rkmpi headers
    ${PROJECT_SOURCE_DIR}/rkmpi/include
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rknn
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/uAPI2
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/common
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/xcore
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/algos
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/iq_parser
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/iq_parser_v2
    ${PROJECT_SOURCE_DIR}/rkmpi/include/rkaiq/smartIr
    # opencv headers
    # ${PROJECT_SOURCE_DIR}/opencv_rv1106_410/include/opencv4/opencv2

)

# 编译选项 
target_compile_options(app PRIVATE
    -Wall
    -Wextra
)


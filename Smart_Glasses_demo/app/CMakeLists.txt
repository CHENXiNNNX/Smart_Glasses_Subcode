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

# 设置头文件包含目录
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/test
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot
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
)

# 编译选项 
target_compile_options(app PRIVATE
    -Wall
    -Wextra
)


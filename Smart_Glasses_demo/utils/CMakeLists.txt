file(GLOB UTILS_SOURCES "*.c")

# 定义 utils 模块为一个静态库
add_library(utils STATIC ${UTILS_SOURCES})

# 设置 utils 的头文件路径
target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
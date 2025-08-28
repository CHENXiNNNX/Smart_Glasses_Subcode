
file(GLOB_RECURSE GUI_APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"  # 查找当前目录及子目录中的所有 .c 文件
)

# 定义 gui_app 模块为一个静态库
add_library(gui_app STATIC ${GUI_APP_SOURCES})

# 设置 gui_app 的头文件路径
target_include_directories(gui_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/common/stack
    ${CMAKE_CURRENT_SOURCE_DIR}/common/page_manager
)

# 查找包
find_package(json-c REQUIRED)
find_package(CURL REQUIRED)
# 将库链接到 gui_app
target_link_libraries(gui_app PRIVATE json-c)
target_include_directories(gui_app PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(gui_app PRIVATE CURL::libcurl)
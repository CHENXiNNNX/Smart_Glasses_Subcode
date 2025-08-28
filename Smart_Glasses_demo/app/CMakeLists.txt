# App module CMakeLists.txt

# 收集当前目录及子目录中的所有 .c 文件
file(GLOB_RECURSE APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# 创建 app 静态库
add_library(app STATIC ${APP_SOURCES})

# 设置头文件包含目录
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot
)

# 链接依赖库 (如果需要的话)
# target_link_libraries(app PRIVATE 
#     # 在这里添加需要链接的库
# )

# 编译选项 
target_compile_options(app PRIVATE
    -Wall
    -Wextra
)

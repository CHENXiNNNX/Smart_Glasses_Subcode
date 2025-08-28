# Add the C wrapper source file
add_library(AIchat-c-interface STATIC
    AIchat_c_interface.cc  # C wrapper implementation
)
# Link the core library into the C wrapper
target_link_libraries(AIchat-c-interface PRIVATE AIChatCore)

# 如果BUILD_TESTS选项被启用，则添加测试可执行文件
if(BUILD_TESTS)
    # 添加测试可执行文件
    add_executable(test_c_interface test.c)
    # 将测试可执行文件与C包装器和核心库链接
    target_link_libraries(test_c_interface PRIVATE AIchat-c-interface AIChatCore)
endif()

# App module CMakeLists.txt

# 收集当前目录及子目录中的所有代码文件
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

# samplerate 依赖
include_directories(${SAMPLERATE_INCLUDE_DIRS})
target_link_libraries(app ${SAMPLERATE_LIBRARIES})

# speexdsp 依赖
target_include_directories(app PRIVATE ${SPEEX_INCLUDE_DIRS})
target_link_directories(app PRIVATE ${SPEEX_LIBRARY_DIRS})
target_link_libraries(app speexdsp)

# 设置头文件包含目录
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # chatbot headers
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/activation
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/mcp
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/protocol_handle
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/statemachine
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/uuid
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/wakeword
    # media headers
    ${CMAKE_CURRENT_SOURCE_DIR}/media
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio/test
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/test
    # protocol headers
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/rtsp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/websocket
    # tool headers
    ${CMAKE_CURRENT_SOURCE_DIR}/tool
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/mac
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/memory
    # libdatachannel headers
    ${PROJECT_SOURCE_DIR}/libdatachannel/include
    ${PROJECT_SOURCE_DIR}/libdatachannel/deps/json/single_include
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
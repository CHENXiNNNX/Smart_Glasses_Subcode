# App module CMakeLists.txt

# 收集当前目录及子目录中的所有代码文件
file(GLOB_RECURSE APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
list(FILTER APP_SOURCES EXCLUDE REGEX ".*copy\\.(cc|cpp|h|hpp)$")
list(FILTER APP_SOURCES EXCLUDE REGEX ".*/audio copy/.*")

# 创建 app 静态库
add_library(app STATIC ${APP_SOURCES})

# opencv 依赖
set(OpenCV_DIR "${PROJECT_SOURCE_DIR}/third_party/opencv_rv1106_410/lib/cmake/opencv4")
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

# snowboy 唤醒词检测依赖
target_include_directories(app PRIVATE ${SNOWBOY_INCLUDE_DIRS})
target_link_libraries(app ${SNOWBOY_LIBRARY})

# 设置头文件包含目录
target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # interfaces
    ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
    # chatbot headers
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/activation
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/mcp
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/mcp/mcp_tool
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/protocol_handle
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/uuid
    ${CMAKE_CURRENT_SOURCE_DIR}/chatbot/wakeword
    # media headers
    ${CMAKE_CURRENT_SOURCE_DIR}/media
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio/core
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio/proc
    ${CMAKE_CURRENT_SOURCE_DIR}/media/audio/util
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/domain
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/pool
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/adapters/isp
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/adapters/encoder
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/adapters/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/explain
    ${CMAKE_CURRENT_SOURCE_DIR}/media/osd
    # network headers
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/network/bluetooth
    ${CMAKE_CURRENT_SOURCE_DIR}/network/lte
    ${CMAKE_CURRENT_SOURCE_DIR}/network/wifi
    # protocol headers
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/http
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/rtsp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/webrtc
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/websocket
    # tool headers
    ${CMAKE_CURRENT_SOURCE_DIR}/tool
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/file
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/log
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/mac
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/media/camera/video_ring_buffer
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/time
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/uuid
    # rknn headers
    ${CMAKE_CURRENT_SOURCE_DIR}/rknn
    ${CMAKE_CURRENT_SOURCE_DIR}/rknn/face_detection
    ${CMAKE_CURRENT_SOURCE_DIR}/rknn/face_recognition
    ${PROJECT_SOURCE_DIR}/third_party/rknpu2/include
    # libdatachannel headers
    ${PROJECT_SOURCE_DIR}/third_party/libdatachannel/include
    ${PROJECT_SOURCE_DIR}/third_party/libdatachannel/deps/json/single_include
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
target_compile_options(app PRIVATE
    -Wall
    -Wextra
)
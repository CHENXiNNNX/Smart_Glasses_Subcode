# Minimum CMake version requirement
cmake_minimum_required(VERSION 3.10)

# Option to choose target architecture
option(TARGET_ARM "Build for ARM architecture" OFF)

# If building for ARM, set the toolchain file before anything else
if(TARGET_ARM)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake" CACHE FILEPATH "Default toolchain file for ARM builds")
        message(STATUS "No toolchain file specified; using default: ${CMAKE_TOOLCHAIN_FILE}")
    endif()
    message(STATUS "Building for ARM using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(STATUS "Building for x86 native architecture.")
endif()

# Project name and language
project(AIChatClient)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure pkg-config is available
find_package(PkgConfig REQUIRED)

if(TARGET_ARM)
    # 设置交叉编译环境下 pkg-config 使用的路径
    set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/pkgconfig")
endif()

# Find required packages
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
find_package(WEBSOCKETPP REQUIRED)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/snowboy)

# Include directories
include_directories(
    ${OPUS_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${WEBSOCKETPP_INCLUDE_DIRS}
)

# Add the source files
add_library(AIChatCore STATIC
    Audio/AudioProcess.cc
    WebSocket/WebsocketClient.cc
    Intent/IntentHandler.cc
    StateMachine/StateMachine.cc

    Application/Application.cc
    Application/WS_Handler.cc
    Application/StateConfig.cc
    Application/IntentsRegistry.cc
    # User defined states
    Application/UserStates/Fault.cc
    Application/UserStates/Startup.cc
    Application/UserStates/Stop.cc
    Application/UserStates/Idle.cc
    Application/UserStates/Listening.cc
    Application/UserStates/Thinking.cc
    Application/UserStates/Speaking.cc
    # User defined intents
    Application/UserIntents/RobotMove.cc
)

# Link libraries for opus and websocketpp
target_link_libraries(AIChatCore
    ${OPUS_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${WEBSOCKETPP_LIBRARIES}
    snowboy-detect-c-wrapper
)

# Add the executable and link it to the core library
add_executable(AIChatClient 
    main.cc
)
target_link_libraries(AIChatClient AIChatCore)

# Add the C interface (cmake details in c_interface/CMakeLists.txt)
add_subdirectory(c_interface)

# Custom command to clean up generated files
if(NOT TARGET clean-all)
    add_custom_target(clean-all
        COMMAND find "${CMAKE_BINARY_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        COMMENT "Cleaning all generated files."
    )
endif()

set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/rv1106_AIChatClient_demo")
# 安装可执行文件到/bin子目录
install(TARGETS AIChatClient DESTINATION bin)

# 安装资源文件到demo/resources子目录
install(FILES ${CMAKE_SOURCE_DIR}/third_party/snowboy/resources/common.res
        DESTINATION third_party/snowboy/resources)
install(FILES ${CMAKE_SOURCE_DIR}/third_party/snowboy/resources/models/echo.pmdl
        DESTINATION third_party/snowboy/resources/models)
install(FILES ${CMAKE_SOURCE_DIR}/third_party/audio/waked.pcm
        DESTINATION third_party/audio)
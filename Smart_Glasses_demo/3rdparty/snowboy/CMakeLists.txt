cmake_minimum_required(VERSION 3.10)

# Find the architecture
set(SNOWBOY_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/lib/rpi/libsnowboy-detect.a")
message(STATUS "Building for Arm native architecture.")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake" CACHE FILEPATH "Default toolchain file for ARM builds")
    message(STATUS "No toolchain file specified; using default: ${CMAKE_TOOLCHAIN_FILE}")
endif()
    message(STATUS "Building for ARM using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")


# Set the project name and define the library
project(SnowboyLib)

add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 搜索所有 .cc 和 .h 文件
file(GLOB_RECURSE SNOWBOY_SOURCES
    #"${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/demo2.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# 添加静态库
add_library(snowboy STATIC ${SNOWBOY_SOURCES})

# Link libraries
target_link_libraries(snowboy PRIVATE ${SNOWBOY_LIBRARY})

# Additional flags and options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

# 确保链接必要的数学库
find_package(OpenBLAS REQUIRED)
if(OpenBLAS_FOUND)
    set(BLAS_LIBRARIES ${OpenBLAS_LIBRARIES})
    set(LAPACK_LIBRARIES ${OpenBLAS_LIBRARIES})  # OpenBLAS 包含 LAPACK 实现
    set(BLAS_INCLUDE_DIRS ${OpenBLAS_INCLUDE_DIR})
endif()


# 将找到的库添加到目标
target_include_directories(snowboy PUBLIC ${BLAS_INCLUDE_DIRS})
target_link_libraries(snowboy PRIVATE ${BLAS_LIBRARIES} ${LAPACK_LIBRARIES})

# Create the C wrapper for snowboy-detect
add_library(snowboy-detect-c-wrapper STATIC
    snowboy-detect-c-wrapper.cc  # C wrapper implementation
)

# Link the snowboy C++ library into the C wrapper
target_link_libraries(snowboy-detect-c-wrapper PRIVATE snowboy)

# 添加一个可选的 demo 可执行文件
if(BUILD_DEMO)
    add_executable(demo2 demo2.cc)
    target_link_libraries(demo2 snowboy)
endif()

cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake" CACHE FILEPATH "Default toolchain file for ARM builds")
    message(STATUS "No toolchain file specified; using default: ${CMAKE_TOOLCHAIN_FILE}")
endif()
message(STATUS "Building for ARM using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

project(rknn_yolov5_demo)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

add_definitions(-DRV1106_1103)

set(LIBRKNNRT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/Linux/armhf-uclibc/librknnmrt.so")

message(STATUS "LIB_RKNNRT = ${LIBRKNNRT}")
set(rknpu2_yolov5_file rknpu2/yolov5_rv1106_1103.cc)



add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils/ utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

#Opencv 4 
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS} )

# Add the source files
add_library(yoloCameraCore STATIC
    postprocess.cc
    AIcamera_c_interface.cc
    ${rknpu2_yolov5_file}
)

# Link libraries for opus and websocketpp
target_link_libraries(yoloCameraCore
    imageutils
    fileutils
    imagedrawing    
    ${LIBRKNNRT}
    ${OpenCV_LIBS}
)

target_include_directories(yoloCameraCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
    ${OpenCV_LIBS}
)

# add_executable(c_face_test main2.cc)
# target_link_libraries(c_face_test yoloCameraCore)
# target_include_directories(c_face_test PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
#     ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
#     ${OpenCV_LIBS}
# )


add_executable(${PROJECT_NAME}
    main.cc
)

target_link_libraries(${PROJECT_NAME} yoloCameraCore)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/allocator/dma
    ${OpenCV_LIBS}
)

set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/rv1106_yolov5_demo")

# 指定运行时动态库的加载路径
# 设置运行时 rpath，以便可执行文件能够在运行时找到指定目录下的共享库
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,\$ORIGIN/lib")

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/bus.jpg DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/coco_80_labels_list.txt DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION ${CMAKE_INSTALL_PREFIX}/model)
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

if(TARGET_ARM)
    # 设置交叉编译环境下 pkg-config 使用的路径
    set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/usr/lib/pkgconfig")
endif()

project(DeskBot)

set(CMAKE_C_STANDARD 99)#C99 # lvgl officially support C99 and above
set(CMAKE_CXX_STANDARD 17)#C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_subdirectory(lvgl)          # LVGL 核心模块
add_subdirectory(gui_app)       # GUI_APP 应用模块
add_subdirectory(utils)         # 公共工具模块
add_subdirectory(common)        # common

# Include 配置头文件路径
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR}/conf)

# 定义主程序源文件
set(MAIN_SOURCES main.c)

# 链接库
add_executable(main ${MAIN_SOURCES})

# 链接自定义的文件夹库
target_link_libraries(
    main 
    gui_app
    utils
    common
)

include(${CMAKE_CURRENT_LIST_DIR}/lvgl/tests/FindLibDRM.cmake)
include_directories(${Libdrm_INCLUDE_DIRS})

find_package(SDL2)
find_package(SDL2_image)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})

target_link_libraries(main lvgl lvgl::examples lvgl::demos lvgl::thorvg ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES} ${Libdrm_LIBRARIES} m pthread)
add_custom_target (run COMMAND ${OUTPUT_PATH}/main DEPENDS main)

# 以下内容用于复制配置文件
add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/utils/system_para.conf
            ${OUTPUT_PATH}/system_para.conf
    COMMENT "Copying system_para.conf to bin directory"
)

add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/utils/gaode_adcode.json
            ${OUTPUT_PATH}/gaode_adcode.json
    COMMENT "Copying gaode_adcode.json to bin directory"
)

add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/utils/cacert.pem
            ${OUTPUT_PATH}/cacert.pem
    COMMENT "Copying cacert.pem to bin directory"
)

# 引用AIChat_demo中的库
set(AI_CHAT_CLIENT_PATH "${PROJECT_SOURCE_DIR}/../AIChat_demo/Client" CACHE PATH "Path to AIChat_demo Client")
add_subdirectory(${AI_CHAT_CLIENT_PATH} ${CMAKE_BINARY_DIR}/AIChat_build)
target_link_libraries(main AIchat-c-interface AIChatCore)

# 复制AIchat所需文件
add_custom_command(
    TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/snowboy/resources/models
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/third_party/audio
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AI_CHAT_CLIENT_PATH}/third_party/snowboy/resources/common.res
            ${OUTPUT_PATH}/third_party/snowboy/resources/common.res
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AI_CHAT_CLIENT_PATH}/third_party/snowboy/resources/models/echo.pmdl
            ${OUTPUT_PATH}/third_party/snowboy/resources/models/echo.pmdl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AI_CHAT_CLIENT_PATH}/third_party/audio/waked.pcm
            ${OUTPUT_PATH}/third_party/audio/waked.pcm
    COMMENT "Copying AI Chat necessary resource files to executable output directory"
)

if(TARGET_ARM)
    # AI (yolo) camera
    set(AI_CAMERA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../yolov5_demo" CACHE PATH "Path to AIChat_demo Client")
    add_subdirectory(${AI_CAMERA_PATH}/cpp ${CMAKE_BINARY_DIR}/AICamera_build)
    target_link_libraries(main yoloCameraCore)
    # 复制yolo模型文件所需
    add_custom_command(
        TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/model # 确保目标目录存在
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${AI_CAMERA_PATH}/model/bus.jpg
                ${OUTPUT_PATH}/model/bus.jpg
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${AI_CAMERA_PATH}/model/coco_80_labels_list.txt
                ${OUTPUT_PATH}/model/coco_80_labels_list.txt
        COMMENT "Copying yolov5 model files to executable output directory"
    )
    # 获取所有.rknn文件
    file(GLOB RKNN_FILES "${AI_CAMERA_PATH}/model/*.rknn")
    foreach(RKNN_FILE ${RKNN_FILES})
        get_filename_component(FILE_NAME ${RKNN_FILE} NAME) # 获取文件名
        add_custom_command(
            TARGET main POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RKNN_FILE}
                    ${OUTPUT_PATH}/model/${FILE_NAME}
            COMMENT "Copying ${FILE_NAME} to executable output directory"
        )
    endforeach()

    # 获取所有需要复制的共享库文件
    set(SHARED_LIBS
        "${AI_CAMERA_PATH}/cpp/3rdparty/librga/Linux/armhf_uclibc/librga.so"
        "${AI_CAMERA_PATH}/cpp/3rdparty/rknpu2/Linux/armhf-uclibc/librknnmrt.so"
    )

    # 创建目标目录
    add_custom_command(
        TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_PATH}/lib
        COMMENT "Creating lib directory for shared libraries"
    )

    # 复制每个共享库文件
    foreach(LIB_FILE ${SHARED_LIBS})
        get_filename_component(FILE_NAME ${LIB_FILE} NAME) # 获取文件名
        add_custom_command(
            TARGET main POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${LIB_FILE}
                    ${OUTPUT_PATH}/lib/${FILE_NAME}
            COMMENT "Copying ${FILE_NAME} to executable output directory"
        )
    endforeach()
    # 设置运行时 rpath，以便可执行文件能够在运行时找到指定目录下的共享库
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,\$ORIGIN/lib")

endif()

# 使用 set_target_properties 设置 main 的输出目录
set_target_properties(main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_PATH}"
)

# Custom command to clean up generated files
if(NOT TARGET clean-all)
    add_custom_target(clean-all
        COMMAND find "${CMAKE_BINARY_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        COMMENT "Cleaning all generated files."
    )
endif()
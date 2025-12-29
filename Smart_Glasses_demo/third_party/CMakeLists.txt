cmake_minimum_required(VERSION 3.15)

project(rknn_model_zoo_3rdparty_libs)

set(TARGET_SOC "rv1106")
set (TARGET_LIB_ARCH armhf_uclibc)

# jpeg_turbo
set(JPEG_TURBO_PATH ${CMAKE_CURRENT_SOURCE_DIR}/jpeg_turbo)
set(JPEG_TURBO_INCLUDES ${JPEG_TURBO_PATH}/include PARENT_SCOPE)
set(LIBTURBOJPEG ${JPEG_TURBO_PATH}/lib_uclibc/libturbojpeg.a PARENT_SCOPE)
install(FILES ${JPEG_TURBO_PATH}/lib_uclibc/libturbojpeg.a DESTINATION lib)

# rknn runtime
set(RKNN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/rknpu2)
set(LIBRKNNRT ${RKNN_PATH}/${CMAKE_SYSTEM_NAME}/armhf-uclibc/librknnmrt.so PARENT_SCOPE) 
set(LIBRKNNRT_INCLUDES ${RKNN_PATH}/include PARENT_SCOPE)
install(FILES ${LIBRKNNRT} DESTINATION lib)

# rga
set(RGA_PATH ${CMAKE_CURRENT_SOURCE_DIR}/librga)
set(LIBRGA ${RGA_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/librga.a PARENT_SCOPE)
set(LIBRGA_INCLUDES ${RGA_PATH}/include PARENT_SCOPE)
install(FILES ${LIBRGA} DESTINATION lib)

# snowboy语音唤醒库
set(SNOWBOY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/snowboy)
set(SNOWBOY_INCLUDE_DIRS ${SNOWBOY_PATH}/include PARENT_SCOPE)
set(SNOWBOY_LIBRARY "snowboy" PARENT_SCOPE) 

# 添加snowboy子目录进行构建
add_subdirectory(snowboy)

# 安装snowboy库文件
install(FILES ${SNOWBOY_PATH}/lib/rpi/libsnowboy-detect.a DESTINATION lib)

# 音频资源文件
set(AUDIO_RESOURCES_PATH ${CMAKE_CURRENT_SOURCE_DIR}/audio)
set(AUDIO_RESOURCES_PATH ${AUDIO_RESOURCES_PATH} PARENT_SCOPE)


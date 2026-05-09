# 测试可执行文件 - 输出到 bin/test 目录

set(TEST_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin/test")
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

# 定义测试可执行文件列表
set(TEST_APPS
    basic.cpp:basic
    test_audio.cpp:test_audio
    test_camera.cpp:test_camera
    test_face.cpp:test_face
    test_mosquito.cpp:test_mosquito
    test_uvc_mosquito.cpp:test_uvc_mosquito
    test_load_rknn.cpp:test_load_rknn
    test_load_onnx.cpp:test_load_onnx
    test_silero_vad_onnx.cpp:test_silero_vad_onnx
    test_mac.cpp:test_mac
    test_memery.cpp:test_memery
    test_opencv.cpp:test_opencv
    test_uvc.cpp:test_uvc
    test_v4l2.cpp:test_v4l2
    test_webrtc.cpp:test_webrtc
    test_wifi.cpp:test_wifi
    test_vl53l7.cpp:test_vl53l7
)

# 需要完整依赖的测试
set(FULL_DEPS_TESTS test_main basic test_audio test_camera test_face test_mosquito test_uvc_mosquito test_mosquito_live test_load_rknn test_mac
    test_memery test_webrtc test_wifi)

# 仅需 OpenCV 的测试
set(OPENCV_ONLY_TESTS test_opencv)

# 仅需 ONNX Runtime 的测试
set(ONNX_ONLY_TESTS test_load_onnx)

# 依赖 app + ONNX Runtime 的测试
set(FULL_DEPS_ONNX_TESTS test_silero_vad_onnx)

# 独立测试 (无 app 依赖)
set(STANDALONE_TESTS test_v4l2 test_uvc)

# 仅依赖 vl53l7 驱动的测试
set(VL53L7_ONLY_TESTS test_vl53l7)

foreach(entry ${TEST_APPS})
    string(REPLACE ":" ";" pair ${entry})
    list(GET pair 0 src)
    list(GET pair 1 name)

    add_executable(${name} ${src})
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TEST_OUTPUT_DIR}")

    if(${name} IN_LIST STANDALONE_TESTS)
        target_link_libraries(${name} pthread)
    elseif(${name} IN_LIST ALSA_LOOPBACK_TESTS)
        target_link_libraries(${name} asound pthread)
    elseif(${name} IN_LIST VL53L7_ONLY_TESTS)
        target_link_libraries(${name} vl53l7 pthread)
    elseif(${name} IN_LIST OPENCV_ONLY_TESTS)
        target_include_directories(${name} PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(${name} ${OpenCV_LIBS})
    elseif(${name} IN_LIST ONNX_ONLY_TESTS)
        target_include_directories(${name} PRIVATE
            ${PROJECT_SOURCE_DIR}/lib/onnxruntime/include/onnxruntime/core/session
        )
        target_link_directories(${name} PRIVATE
            ${PROJECT_SOURCE_DIR}/lib/onnxruntime
        )
        set_target_properties(${name} PROPERTIES
            BUILD_RPATH "\$ORIGIN/../lib/onnxruntime"
            INSTALL_RPATH "\$ORIGIN/../lib/onnxruntime"
        )
        target_link_libraries(${name}
            onnxruntime
            pthread
            dl
            m
            stdc++fs
        )
    elseif(${name} IN_LIST FULL_DEPS_ONNX_TESTS)
        target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
        target_link_libraries(${name}
            utils
            app
            datachannel-static
        )
        target_link_libraries(${name}
            ${OPUS_LIBRARIES}
            ${JSONCPP_LIBRARIES}
            ${SAMPLERATE_LIBRARIES}
            ${WEBSOCKETPP_LIBRARIES}
            ${CURL_LIBRARIES}
            snowboy-detect-c-wrapper
        )
        target_include_directories(${name} PRIVATE ${SNOWBOY_INCLUDE_DIRS})
        target_link_libraries(${name} ${OpenCV_LIBS})
        target_include_directories(${name} PRIVATE ${JPEG_TURBO_INCLUDES})
        target_link_libraries(${name} ${LIBTURBOJPEG})
        target_include_directories(${name} PRIVATE ${LIBRKNNRT_INCLUDES})
        target_link_libraries(${name} ${LIBRKNNRT})
        target_link_directories(${name} PRIVATE
            ${ROCKIT_LIB}
            ${RKAIQ_LIB}
            ${RGA_LIB}
            ${MPP_LIB}
            ${IVA_LIB}
            ${SAMPLE_COMM_LIB}
            ${RTSP_LIB}
            ${PROJECT_SOURCE_DIR}/lib/onnxruntime
        )
        target_include_directories(${name} PRIVATE
            ${ROCKIT_INC}
            ${RKAIQ_INC}
            ${RKAIQ_INC}/rkaiq/uAPI2
            ${RKAIQ_INC}/rkaiq/common
            ${RKAIQ_INC}/rkaiq/xcore
            ${RKAIQ_INC}/rkaiq/algos
            ${RKAIQ_INC}/rkaiq/iq_parser
            ${RKAIQ_INC}/rkaiq/iq_parser_v2
            ${RGA_INC}
            ${MPP_INC}
            ${IVA_INC}
            ${SAMPLES_ROOT}
            ${SAMPLES_ROOT}/isp3.x
            ${PROJECT_SOURCE_DIR}/third_party/libdatachannel/deps/json/single_include
            ${PROJECT_SOURCE_DIR}/lib/onnxruntime/include/onnxruntime/core/session
        )
        set_target_properties(${name} PROPERTIES
            BUILD_RPATH "\$ORIGIN/../lib/onnxruntime"
            INSTALL_RPATH "\$ORIGIN/../lib/onnxruntime"
        )
        target_link_libraries(${name}
            rockit_full
            rkaiq
            rga
            rockchip_mpp
            rockiva
            sample_comm
            rtsp
            onnxruntime
            asound
            pthread
            dl
            m
            stdc++fs
        )
    else()
        target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
        target_link_libraries(${name}
            utils
            app
            datachannel-static
        )
        target_link_libraries(${name}
            ${OPUS_LIBRARIES}
            ${JSONCPP_LIBRARIES}
            ${SAMPLERATE_LIBRARIES}
            ${WEBSOCKETPP_LIBRARIES}
            ${CURL_LIBRARIES}
            snowboy-detect-c-wrapper
        )
        target_include_directories(${name} PRIVATE ${SNOWBOY_INCLUDE_DIRS})
        target_link_libraries(${name} ${OpenCV_LIBS})
        target_include_directories(${name} PRIVATE ${JPEG_TURBO_INCLUDES})
        target_link_libraries(${name} ${LIBTURBOJPEG})
        target_include_directories(${name} PRIVATE ${LIBRKNNRT_INCLUDES})
        target_link_libraries(${name} ${LIBRKNNRT})
        target_link_directories(${name} PRIVATE
            ${ROCKIT_LIB}
            ${RKAIQ_LIB}
            ${RGA_LIB}
            ${MPP_LIB}
            ${IVA_LIB}
            ${SAMPLE_COMM_LIB}
            ${RTSP_LIB}
        )
        target_include_directories(${name} PRIVATE
            ${ROCKIT_INC}
            ${RKAIQ_INC}
            ${RKAIQ_INC}/rkaiq/uAPI2
            ${RKAIQ_INC}/rkaiq/common
            ${RKAIQ_INC}/rkaiq/xcore
            ${RKAIQ_INC}/rkaiq/algos
            ${RKAIQ_INC}/rkaiq/iq_parser
            ${RKAIQ_INC}/rkaiq/iq_parser_v2
            ${RGA_INC}
            ${MPP_INC}
            ${IVA_INC}
            ${SAMPLES_ROOT}
            ${SAMPLES_ROOT}/isp3.x
        )
        target_link_libraries(${name}
            rockit_full
            rkaiq
            rga
            rockchip_mpp
            rockiva
            sample_comm
            rtsp
            asound
            pthread
            dl
            m
            stdc++fs
        )
        target_include_directories(${name} PRIVATE
            ${PROJECT_SOURCE_DIR}/third_party/libdatachannel/deps/json/single_include
        )
    endif()
endforeach()

# test 默认使用 /root/bin 下资源
if(TARGET test_audio)
    target_compile_definitions(test_audio PRIVATE
        "AUDIO_RK_VQE_CONFIG_PATH=\"/root/bin/assets/conf/config_aivqe.json\"")
endif()

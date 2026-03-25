# 测试可执行文件 - 输出到 bin/test 目录

set(TEST_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin/test")
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})

# 定义测试可执行文件列表
set(TEST_APPS
    test_main.cpp:test_main
    basic_main.cpp:basic
    test_audio_main.cpp:test_audio
    test_camera_main.cpp:test_camera
    test_face_main.cpp:test_face
    test_load_model_main.cpp:test_load_model
    test_mac_main.cpp:test_mac
    test_memery_main.cpp:test_memery
    test_opencv_main.cpp:test_opencv
    test_v4l2_main.cpp:test_v4l2
    test_webrtc_main.cpp:test_webrtc
    test_wifi_main.cpp:test_wifi
)

# 需要完整依赖的测试
set(FULL_DEPS_TESTS test_main basic test_audio test_camera test_face test_load_model test_mac
    test_memery test_webrtc test_wifi)

# 仅需 OpenCV 的测试
set(OPENCV_ONLY_TESTS test_opencv)

# 独立测试 (无 app 依赖)
set(STANDALONE_TESTS test_v4l2)

foreach(entry ${TEST_APPS})
    string(REPLACE ":" ";" pair ${entry})
    list(GET pair 0 src)
    list(GET pair 1 name)

    add_executable(${name} ${src})
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TEST_OUTPUT_DIR}")

    if(${name} IN_LIST STANDALONE_TESTS)
        target_link_libraries(${name} pthread)
    elseif(${name} IN_LIST OPENCV_ONLY_TESTS)
        target_include_directories(${name} PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(${name} ${OpenCV_LIBS})
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

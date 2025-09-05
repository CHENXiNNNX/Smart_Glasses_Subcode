# Install script for directory: /home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/irex/WorkSpace/Smart_Glasses/SDK/rv1106-sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/usrsctp/usrsctplib/libusrsctp.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/libsrtp2.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libjuice/libjuice.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/libdatachannel.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/rtc" TYPE FILE FILES
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/candidate.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/channel.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/configuration.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/datachannel.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/dependencydescriptor.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/description.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/iceudpmuxlistener.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/mediahandler.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtcpreceivingsession.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/common.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/global.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/message.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/frameinfo.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/peerconnection.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/reliability.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtc.h"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtc.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtp.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/track.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/websocket.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/websocketserver.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtppacketizationconfig.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtcpsrreporter.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtppacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtpdepacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/h264rtppacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/h264rtpdepacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/nalunit.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/h265rtppacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/h265rtpdepacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/h265nalunit.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/av1rtppacketizer.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rtcpnackresponder.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/utils.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/plihandler.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/pacinghandler.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/rembhandler.hpp"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/include/rtc/version.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake"
         "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/CMakeFiles/Export/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel" TYPE FILE FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/CMakeFiles/Export/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^()$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel" TYPE FILE FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/CMakeFiles/Export/lib/cmake/LibDataChannel/LibDataChannelTargets-noconfig.cmake")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel" TYPE FILE FILES
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/LibDataChannelConfig.cmake"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/LibDataChannelConfigVersion.cmake"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/client/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/client-benchmark/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/media-receiver/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/media-sender/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/media-sfu/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/streamer/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/copy-paste/cmake_install.cmake")
  include("/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/examples/copy-paste-capi/cmake_install.cmake")

endif()


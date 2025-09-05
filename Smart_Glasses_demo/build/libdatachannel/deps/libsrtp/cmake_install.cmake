# Install script for directory: /home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/deps/libsrtp

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
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/libsrtp2.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/srtp2" TYPE FILE FILES
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/deps/libsrtp/include/srtp.h"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/deps/libsrtp/crypto/include/auth.h"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/deps/libsrtp/crypto/include/cipher.h"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/libdatachannel/deps/libsrtp/crypto/include/crypto_types.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP/libSRTPTargets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP/libSRTPTargets.cmake"
         "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/CMakeFiles/Export/lib/cmake/libSRTP/libSRTPTargets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP/libSRTPTargets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP/libSRTPTargets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP" TYPE FILE FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/CMakeFiles/Export/lib/cmake/libSRTP/libSRTPTargets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP" TYPE FILE FILES "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/CMakeFiles/Export/lib/cmake/libSRTP/libSRTPTargets-debug.cmake")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/libSRTP" TYPE FILE FILES
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/libSRTPConfig.cmake"
    "/home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo/build/libdatachannel/deps/libsrtp/libSRTPConfigVersion.cmake"
    )
endif()


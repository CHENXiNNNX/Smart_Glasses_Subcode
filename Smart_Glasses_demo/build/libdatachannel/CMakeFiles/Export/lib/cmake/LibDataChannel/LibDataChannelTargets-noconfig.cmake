#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LibDataChannel::usrsctp" for configuration ""
set_property(TARGET LibDataChannel::usrsctp APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(LibDataChannel::usrsctp PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libusrsctp.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS LibDataChannel::usrsctp )
list(APPEND _IMPORT_CHECK_FILES_FOR_LibDataChannel::usrsctp "${_IMPORT_PREFIX}/lib/libusrsctp.a" )

# Import target "LibDataChannel::srtp2" for configuration ""
set_property(TARGET LibDataChannel::srtp2 APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(LibDataChannel::srtp2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libsrtp2.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS LibDataChannel::srtp2 )
list(APPEND _IMPORT_CHECK_FILES_FOR_LibDataChannel::srtp2 "${_IMPORT_PREFIX}/lib/libsrtp2.a" )

# Import target "LibDataChannel::LibJuice" for configuration ""
set_property(TARGET LibDataChannel::LibJuice APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(LibDataChannel::LibJuice PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libjuice.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS LibDataChannel::LibJuice )
list(APPEND _IMPORT_CHECK_FILES_FOR_LibDataChannel::LibJuice "${_IMPORT_PREFIX}/lib/libjuice.a" )

# Import target "LibDataChannel::LibDataChannel" for configuration ""
set_property(TARGET LibDataChannel::LibDataChannel APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(LibDataChannel::LibDataChannel PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libdatachannel.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS LibDataChannel::LibDataChannel )
list(APPEND _IMPORT_CHECK_FILES_FOR_LibDataChannel::LibDataChannel "${_IMPORT_PREFIX}/lib/libdatachannel.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

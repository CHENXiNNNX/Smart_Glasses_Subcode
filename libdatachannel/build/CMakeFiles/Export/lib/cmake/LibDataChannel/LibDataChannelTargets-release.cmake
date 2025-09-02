#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LibDataChannel::LibDataChannel" for configuration "Release"
set_property(TARGET LibDataChannel::LibDataChannel APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LibDataChannel::LibDataChannel PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libdatachannel.so.0.23.1"
  IMPORTED_SONAME_RELEASE "libdatachannel.so.0.23"
  )

list(APPEND _IMPORT_CHECK_TARGETS LibDataChannel::LibDataChannel )
list(APPEND _IMPORT_CHECK_FILES_FOR_LibDataChannel::LibDataChannel "${_IMPORT_PREFIX}/lib/libdatachannel.so.0.23.1" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

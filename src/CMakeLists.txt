add_library(hydroid-res
  core/Complex.cpp
  core/Value.cpp
  core/ValueParser.cpp
  core/Version.cpp
  capi/hydroid_res.cpp)
add_library(hydroid::res ALIAS hydroid-res)

# The version is project() alone (#13): the headers get it from VersionNumber.h, generated next
# to them in the build tree, and version() from those headers. VersionNumber.h and the SOVERSION
# below need major.minor.patch, each a C integer literal (#44 review).
if(NOT PROJECT_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
  message(FATAL_ERROR "project() VERSION must be major.minor.patch, not '${PROJECT_VERSION}'")
endif()
set(hydroid_res_generated_include ${PROJECT_BINARY_DIR}/include)
configure_file(${PROJECT_SOURCE_DIR}/cmake/VersionNumber.h.in
  ${hydroid_res_generated_include}/hydroid/res/VersionNumber.h @ONLY)

target_include_directories(hydroid-res
  PUBLIC  ${PROJECT_SOURCE_DIR}/include ${hydroid_res_generated_include}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hydroid-res PRIVATE pugixml::pugixml)
target_compile_features(hydroid-res PUBLIC cxx_std_17)

# Static or shared as BUILD_SHARED_LIBS says (#12). Only what include/ marks HYDROID_RES_API is
# exported: hidden visibility makes that so on Linux and macOS too, so a missing mark fails the
# link there as it does on Windows.
get_target_property(hydroid_res_type hydroid-res TYPE)
if(hydroid_res_type STREQUAL "SHARED_LIBRARY")
  target_compile_definitions(hydroid-res PUBLIC HYDROID_RES_SHARED PRIVATE HYDROID_RES_BUILDING)
endif()
set_target_properties(hydroid-res PROPERTIES
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

# The shared library's file version, and the ABI version in its soname (#13). Every 0.x minor
# release may break the ABI, so until 1.0 that is major.minor (libhydroid-res.so.0.1), and the
# major alone after.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(hydroid_res_soversion ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
else()
  set(hydroid_res_soversion ${PROJECT_VERSION_MAJOR})
endif()
set_target_properties(hydroid-res PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${hydroid_res_soversion})
hydroid_set_warnings(hydroid-res)

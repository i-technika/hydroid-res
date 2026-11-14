add_library(hydroid-res
  core/Complex.cpp
  core/Value.cpp
  core/ValueParser.cpp
  core/Version.cpp
  capi/hydroid_res.cpp)
add_library(hydroid::res ALIAS hydroid-res)

target_include_directories(hydroid-res
  PUBLIC  ${PROJECT_SOURCE_DIR}/include
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
hydroid_set_warnings(hydroid-res)

add_library(hydroid-res
  core/Version.cpp
  capi/hydroid_res.cpp)
add_library(hydroid::res ALIAS hydroid-res)

target_include_directories(hydroid-res
  PUBLIC  ${PROJECT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hydroid-res PUBLIC pugixml::pugixml)
target_compile_features(hydroid-res PUBLIC cxx_std_17)
hydroid_set_warnings(hydroid-res)

add_executable(hydroid-rc main.cpp)
target_link_libraries(hydroid-rc PRIVATE hydroid::res)
# main.cpp has wmain on Windows (#87), which MSVC starts by itself and MinGW only with -municode.
if(MINGW)
  target_link_options(hydroid-rc PRIVATE -municode)
endif()
hydroid_set_warnings(hydroid-rc)

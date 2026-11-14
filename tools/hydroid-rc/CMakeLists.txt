add_executable(hydroid-rc main.cpp)
target_link_libraries(hydroid-rc PRIVATE hydroid::res)
hydroid_set_warnings(hydroid-rc)

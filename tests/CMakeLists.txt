include(GoogleTest)

add_executable(hydroid-res-tests
  SmokeTest.cpp)
target_link_libraries(hydroid-res-tests PRIVATE hydroid::res GTest::gtest_main)
target_compile_definitions(hydroid-res-tests PRIVATE
  HYDROID_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/golden/data"
  HYDROID_TEST_RES_DIR="${PROJECT_SOURCE_DIR}/golden/app/src/main/res")
hydroid_set_warnings(hydroid-res-tests)

gtest_discover_tests(hydroid-res-tests DISCOVERY_MODE PRE_TEST)

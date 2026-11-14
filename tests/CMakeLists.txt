include(GoogleTest)

add_executable(hydroid-res-tests
  Aapt2ValueTest.cpp
  ComplexTest.cpp
  GoldenColorTest.cpp
  GoldenComplexTest.cpp
  SmokeTest.cpp
  ValueParserTest.cpp
  ValueTest.cpp)
target_link_libraries(hydroid-res-tests PRIVATE
  hydroid::res GTest::gtest_main nlohmann_json::nlohmann_json)
target_compile_definitions(hydroid-res-tests PRIVATE
  HYDROID_AAPT2_VALUES="${PROJECT_SOURCE_DIR}/golden/aapt2/values.tsv"
  HYDROID_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/golden/data"
  HYDROID_TEST_RES_DIR="${PROJECT_SOURCE_DIR}/golden/app/src/main/res")
hydroid_set_warnings(hydroid-res-tests)

gtest_discover_tests(hydroid-res-tests DISCOVERY_MODE PRE_TEST)

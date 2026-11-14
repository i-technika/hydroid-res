include(GoogleTest)

add_executable(hydroid-res-tests
  Aapt2ConfigTest.cpp
  Aapt2ValueTest.cpp
  ComplexTest.cpp
  ConfigParserTest.cpp
  GoldenColorTest.cpp
  GoldenComplexTest.cpp
  PublicHeaderTest.cpp
  SmokeTest.cpp
  ValueParserTest.cpp
  ValueTest.cpp)
target_link_libraries(hydroid-res-tests PRIVATE
  hydroid::res pugixml::pugixml GTest::gtest_main nlohmann_json::nlohmann_json)
# Every directory a consumer of hydroid::res includes from, for PublicHeaders.AreAscii.
set(public_include_dirs "$<JOIN:$<TARGET_PROPERTY:hydroid-res,INTERFACE_INCLUDE_DIRECTORIES>,|>")
target_compile_definitions(hydroid-res-tests PRIVATE
  HYDROID_AAPT2_CONFIGS="${PROJECT_SOURCE_DIR}/golden/aapt2/configs.tsv"
  HYDROID_AAPT2_VALUES="${PROJECT_SOURCE_DIR}/golden/aapt2/values.tsv"
  HYDROID_AOSP_RES_DIRS="${PROJECT_SOURCE_DIR}/golden/aosp/framework_res_dirs.txt"
  HYDROID_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/golden/data"
  HYDROID_PROJECT_VERSION="${PROJECT_VERSION}"
  HYDROID_PUBLIC_INCLUDE_DIRS="${public_include_dirs}"
  HYDROID_TEST_RES_DIR="${PROJECT_SOURCE_DIR}/golden/app/src/main/res")
if(HYDROID_RES_REQUIRE_LOCALE_TESTS)
  target_compile_definitions(hydroid-res-tests PRIVATE HYDROID_REQUIRE_LOCALE_TESTS)
endif()
hydroid_set_warnings(hydroid-res-tests)

gtest_discover_tests(hydroid-res-tests DISCOVERY_MODE PRE_TEST)

# pugixml is an implementation detail of hydroid::res, so its consumers must not see it (#10).
add_test(NAME CMake.PugixmlStaysPrivate
  COMMAND ${CMAKE_COMMAND}
    "-DCONSUMER_INCLUDES=$<JOIN:$<TARGET_PROPERTY:hydroid-res,INTERFACE_INCLUDE_DIRECTORIES>,|>"
    "-DPUGIXML_INCLUDES=$<JOIN:$<TARGET_PROPERTY:pugixml::pugixml,INTERFACE_INCLUDE_DIRECTORIES>,|>"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckPrivateDeps.cmake)

# BUILD_SHARED_LIBS as hydroid-res was asked for it before the dependencies, so one that
# Dependencies.cmake failed to restore after pugixml shows up here (#12).
if(hydroid_res_fetched_pugixml)
  set(pugixml_links "$<TARGET_PROPERTY:pugixml::pugixml,INTERFACE_LINK_LIBRARIES>")
else()
  set(pugixml_links "")
endif()
add_test(NAME CMake.LibraryTypes
  COMMAND ${CMAKE_COMMAND}
    "-DSHARED=${hydroid_res_shared_requested}"
    "-DFETCHED_PUGIXML=${hydroid_res_fetched_pugixml}"
    "-DHYDROID_RES_TYPE=$<TARGET_PROPERTY:hydroid-res,TYPE>"
    "-DPUGIXML_LINKS=${pugixml_links}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckLibraryTypes.cmake)

# The shared library's file and soname versions come from project() too (#13).
add_test(NAME CMake.LibraryVersion
  COMMAND ${CMAKE_COMMAND}
    -DPROJECT_VERSION=${PROJECT_VERSION}
    "-DVERSION=$<TARGET_PROPERTY:hydroid-res,VERSION>"
    "-DSOVERSION=$<TARGET_PROPERTY:hydroid-res,SOVERSION>"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckLibraryVersion.cmake)

# The dependencies come from where HYDROID_RES_USE_SYSTEM_DEPS says, never silently from the
# system (#11).
set(dependency_sources "")
foreach(dep IN ITEMS pugixml::pugixml GTest::gtest_main nlohmann_json::nlohmann_json)
  get_target_property(imported ${dep} IMPORTED)
  list(APPEND dependency_sources "${dep}=${imported}")
endforeach()
string(JOIN "|" dependency_sources ${dependency_sources})
add_test(NAME CMake.DependenciesFollowOption
  COMMAND ${CMAKE_COMMAND}
    -DUSE_SYSTEM_DEPS=${HYDROID_RES_USE_SYSTEM_DEPS}
    "-DDEPENDENCIES=${dependency_sources}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckDependencySource.cmake)

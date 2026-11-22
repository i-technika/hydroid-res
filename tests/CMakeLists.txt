include(GoogleTest)

add_executable(hydroid-res-tests
  Aapt2ConfigTest.cpp
  Aapt2TableTest.cpp
  Aapt2ValueTest.cpp
  CApiTest.cpp
  ComplexTest.cpp
  ConfigMatchTest.cpp
  ConfigParserTest.cpp
  GoldenComplexTest.cpp
  GoldenLocaleConfigTest.cpp
  GoldenMatchTest.cpp
  PathUtf8Test.cpp
  PublicHeaderTest.cpp
  ResourceTableTest.cpp
  ResourcesTest.cpp
  SdkBaselineTest.cpp
  SmokeTest.cpp
  Utf8Test.cpp
  ValueParserTest.cpp
  ValueTest.cpp
  WhitespaceTest.cpp)
# src/ for the internal headers some tests use, as core/FieldCount.h.
target_include_directories(hydroid-res-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hydroid-res-tests PRIVATE
  hydroid::res pugixml::pugixml GTest::gtest_main nlohmann_json::nlohmann_json)
# Every directory a consumer of hydroid::res includes from, for PublicHeaders.AreAscii.
set(public_include_dirs "$<JOIN:$<TARGET_PROPERTY:hydroid-res,INTERFACE_INCLUDE_DIRECTORIES>,|>")
target_compile_definitions(hydroid-res-tests PRIVATE
  HYDROID_AAPT2_APP_TABLE="${PROJECT_SOURCE_DIR}/golden/aapt2/app_table.txt"
  HYDROID_AAPT2_CONFIGS="${PROJECT_SOURCE_DIR}/golden/aapt2/configs.tsv"
  HYDROID_AAPT2_SCRIPT="${PROJECT_SOURCE_DIR}/scripts/golden/gen_aapt2_values.py"
  HYDROID_AAPT2_STRINGS="${PROJECT_SOURCE_DIR}/golden/aapt2/strings.json"
  HYDROID_AAPT2_TABLE="${PROJECT_SOURCE_DIR}/golden/aapt2/table.txt"
  HYDROID_AAPT2_TABLE_CASES="${PROJECT_SOURCE_DIR}/golden/aapt2/table_cases.json"
  HYDROID_AAPT2_VALUES="${PROJECT_SOURCE_DIR}/golden/aapt2/values.tsv"
  HYDROID_AOSP_RES_DIRS="${PROJECT_SOURCE_DIR}/golden/aosp/framework_res_dirs.txt"
  HYDROID_GOLDEN_APP_GRADLE="${PROJECT_SOURCE_DIR}/golden/app/build.gradle.kts"
  HYDROID_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/golden/data"
  HYDROID_GOLDEN_LOCALECONFIG_DIR="${PROJECT_SOURCE_DIR}/golden/localeconfig/data"
  HYDROID_GOLDEN_LOCALECONFIG_GRADLE="${PROJECT_SOURCE_DIR}/golden/localeconfig/build.gradle.kts"
  HYDROID_LOCALE_SCRIPT="${PROJECT_SOURCE_DIR}/scripts/gen_locale_data.py"
  HYDROID_LOCALE_TABLES="${PROJECT_SOURCE_DIR}/src/core/LocaleDataTables.inc"
  HYDROID_LOCALECONFIG_RES_DIR="${PROJECT_SOURCE_DIR}/golden/localeconfig/src/main/res"
  HYDROID_NUMERIC_QUALIFIERS="${PROJECT_SOURCE_DIR}/scripts/golden/numeric_qualifiers.tsv"
  HYDROID_PROJECT_VERSION="${PROJECT_VERSION}"
  HYDROID_PUBLIC_INCLUDE_DIRS="${public_include_dirs}"
  HYDROID_README="${PROJECT_SOURCE_DIR}/README.md"
  HYDROID_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  HYDROID_TABLE_RES_DIR="${PROJECT_SOURCE_DIR}/golden/table/res"
  HYDROID_TEST_RES_DIR="${PROJECT_SOURCE_DIR}/golden/app/src/main/res"
  HYDROID_VERSION_HEADER="${PROJECT_SOURCE_DIR}/include/hydroid/res/Version.h")
if(HYDROID_RES_REQUIRE_LOCALE_TESTS)
  target_compile_definitions(hydroid-res-tests PRIVATE HYDROID_REQUIRE_LOCALE_TESTS)
endif()
hydroid_set_warnings(hydroid-res-tests)

gtest_discover_tests(hydroid-res-tests DISCOVERY_MODE PRE_TEST)

# The C API from a C program, which hydroid_res.h must compile in as C99 (#203).
add_executable(hydroid-res-capi-c-test CApiCTest.c)
target_link_libraries(hydroid-res-capi-c-test PRIVATE hydroid::res)
set_target_properties(hydroid-res-capi-c-test PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
hydroid_set_warnings(hydroid-res-capi-c-test)
add_test(NAME CApi.WorksFromC
  COMMAND hydroid-res-capi-c-test ${PROJECT_SOURCE_DIR}/golden/table/res)

# What Resources allocates, counted by an operator new of the program's own (#240), so in a
# program of its own: hydroid-res-tests keeps the sanitizers' operator new, which checks that each
# delete is of a new. Not in the sanitizers' builds, as theirs would be replaced.
if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
  add_executable(hydroid-res-alloc-tests ResourcesAllocTest.cpp)
  target_link_libraries(hydroid-res-alloc-tests PRIVATE hydroid::res pugixml::pugixml GTest::gtest_main)
  # A DLL has its own operator new, so the tests skip what they cannot count there.
  target_compile_definitions(hydroid-res-alloc-tests PRIVATE
    "HYDROID_SHARED_LIBRARY=$<STREQUAL:$<TARGET_PROPERTY:hydroid-res,TYPE>,SHARED_LIBRARY>")
  hydroid_set_warnings(hydroid-res-alloc-tests)
  gtest_discover_tests(hydroid-res-alloc-tests DISCOVERY_MODE PRE_TEST)
endif()

# pugixml and expat are implementation details of hydroid::res, so its consumers must not see
# them (#10).
add_test(NAME CMake.DependenciesStayPrivate
  COMMAND ${CMAKE_COMMAND}
    "-DCONSUMER_INCLUDES=$<JOIN:$<TARGET_PROPERTY:hydroid-res,INTERFACE_INCLUDE_DIRECTORIES>,|>"
    "-DPUGIXML_INCLUDES=$<JOIN:$<TARGET_PROPERTY:pugixml::pugixml,INTERFACE_INCLUDE_DIRECTORIES>,|>"
    "-DEXPAT_INCLUDES=$<JOIN:$<TARGET_PROPERTY:expat::expat,INTERFACE_INCLUDE_DIRECTORIES>,|>"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckPrivateDeps.cmake)

# BUILD_SHARED_LIBS as hydroid-res was asked for it before the dependencies, so one that
# Dependencies.cmake failed to restore after pugixml shows up here (#12).
if(hydroid_res_fetched_pugixml)
  set(pugixml_links "$<TARGET_PROPERTY:pugixml::pugixml,INTERFACE_LINK_LIBRARIES>")
else()
  set(pugixml_links "")
endif()
if(hydroid_res_fetched_expat)
  set(expat_type "$<TARGET_PROPERTY:expat::expat,TYPE>")
else()
  set(expat_type "")
endif()
add_test(NAME CMake.LibraryTypes
  COMMAND ${CMAKE_COMMAND}
    "-DSHARED=${hydroid_res_shared_requested}"
    "-DFETCHED_PUGIXML=${hydroid_res_fetched_pugixml}"
    "-DHYDROID_RES_TYPE=$<TARGET_PROPERTY:hydroid-res,TYPE>"
    "-DPUGIXML_LINKS=${pugixml_links}"
    "-DFETCHED_EXPAT=${hydroid_res_fetched_expat}"
    "-DEXPAT_TYPE=${expat_type}"
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
foreach(dep IN ITEMS pugixml::pugixml expat::expat GTest::gtest_main nlohmann_json::nlohmann_json)
  get_target_property(imported ${dep} IMPORTED)
  list(APPEND dependency_sources "${dep}=${imported}")
endforeach()
string(JOIN "|" dependency_sources ${dependency_sources})
add_test(NAME CMake.DependenciesFollowOption
  COMMAND ${CMAKE_COMMAND}
    -DUSE_SYSTEM_DEPS=${HYDROID_RES_USE_SYSTEM_DEPS}
    "-DDEPENDENCIES=${dependency_sources}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckDependencySource.cmake)

# hydroid-rc opens a res/ folder whose path has characters outside the ANSI code page (#87).
if(TARGET hydroid-rc)
  add_test(NAME HydroidRc.DumpsUnicodePath
    COMMAND ${CMAKE_COMMAND}
      "-DRC=$<TARGET_FILE:hydroid-rc>"
      "-DDIR=${CMAKE_CURRENT_BINARY_DIR}/rc-unicode-path"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckRcUnicodePath.cmake)
  # hydroid-rc pick prints what Android picked of the golden app under a condition (#202).
  add_test(NAME HydroidRc.PicksWhatAndroidPicked
    COMMAND ${CMAKE_COMMAND}
      "-DRC=$<TARGET_FILE:hydroid-rc>"
      "-DRES=${PROJECT_SOURCE_DIR}/golden/app/src/main/res"
      "-DGOLDEN=${PROJECT_SOURCE_DIR}/golden/data"
      "-DTABLE=${PROJECT_SOURCE_DIR}/golden/table/res"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckRcPick.cmake)
  # And ends with an error, not an abort, on an argument with no UTF-8, which only Windows passes.
  if(WIN32)
    add_test(NAME HydroidRc.FailsOnNonUnicodeArgument
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/CheckRcNotUnicode.ps1 -Rc $<TARGET_FILE:hydroid-rc>)
  endif()
endif()

# Config.cpp's numbers are the NDK's AConfiguration, and every value of Config.h has one (#53).
# It needs an NDK: HYDROID_RES_ANDROID_NDK, which must hold one, else ANDROID_NDK_HOME,
# ANDROID_NDK_ROOT or the newest under the SDK's ndk/. Without one, or without Python, the test is
# left out, or configure fails with HYDROID_RES_REQUIRE_NDK_TESTS.
set(HYDROID_RES_ANDROID_NDK "" CACHE PATH "An Android NDK for AConfiguration.MatchesNdk")
set(ndk_candidates "$ENV{ANDROID_NDK_HOME}" "$ENV{ANDROID_NDK_ROOT}")
foreach(sdk IN ITEMS "$ENV{ANDROID_HOME}" "$ENV{ANDROID_SDK_ROOT}" "$ENV{LOCALAPPDATA}/Android/Sdk")
  file(GLOB sdk_ndks LIST_DIRECTORIES true "${sdk}/ndk/*")
  list(SORT sdk_ndks COMPARE NATURAL ORDER DESCENDING)
  list(APPEND ndk_candidates ${sdk_ndks})
endforeach()
set(ndk_header "toolchains/llvm/prebuilt/*/sysroot/usr/include/android/configuration.h")
if(HYDROID_RES_ANDROID_NDK)
  file(GLOB found "${HYDROID_RES_ANDROID_NDK}/${ndk_header}")
  if(NOT found)
    message(FATAL_ERROR "HYDROID_RES_ANDROID_NDK has no android/configuration.h: "
      "${HYDROID_RES_ANDROID_NDK}")
  endif()
endif()
set(ndk_configuration_h "")
foreach(ndk IN LISTS HYDROID_RES_ANDROID_NDK ndk_candidates)
  file(GLOB found "${ndk}/${ndk_header}")
  if(ndk AND found)
    list(GET found 0 ndk_configuration_h)
    break()
  endif()
endforeach()
find_package(Python3 COMPONENTS Interpreter)
if(ndk_configuration_h AND Python3_Interpreter_FOUND)
  message(STATUS "AConfiguration.MatchesNdk: ${ndk_configuration_h}")
  add_test(NAME AConfiguration.MatchesNdk
    COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/check_aconfiguration.py
      ${ndk_configuration_h} ${PROJECT_SOURCE_DIR}/include/hydroid/res/Config.h
      ${PROJECT_SOURCE_DIR}/src/core/Config.cpp)
elseif(HYDROID_RES_REQUIRE_NDK_TESTS)
  message(FATAL_ERROR "AConfiguration.MatchesNdk needs an NDK and Python "
    "(HYDROID_RES_REQUIRE_NDK_TESTS is ON); set HYDROID_RES_ANDROID_NDK")
else()
  message(STATUS "AConfiguration.MatchesNdk: no NDK or no Python, left out")
endif()

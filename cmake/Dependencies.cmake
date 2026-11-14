include(FetchContent)

# Every dependency is fetched at a pinned version, so what is installed on the machine never
# changes the build (#11). HYDROID_RES_USE_SYSTEM_DEPS uses installed packages instead, but only
# at exactly those versions, and fails rather than falling back to a fetch.
option(HYDROID_RES_USE_SYSTEM_DEPS
  "Use installed pugixml, GoogleTest and nlohmann/json, at exactly the pinned versions" OFF)

if(NOT HYDROID_RES_USE_SYSTEM_DEPS)
  # FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS would otherwise find_package() any version.
  set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
endif()

# The dependencies' options are set as plain variables rather than FORCEd cache entries, so a
# parent project's cache is left alone. Plain variables only override option() under CMP0077,
# which pugixml's cmake_minimum_required(VERSION 3.5) leaves unset.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# pugixml: the only runtime dependency of the core (values, layout, drawable XML).
if(HYDROID_RES_USE_SYSTEM_DEPS)
  find_package(pugixml 1.14 EXACT REQUIRED CONFIG)
else()
  set(PUGIXML_NO_EXCEPTIONS OFF)
  set(PUGIXML_BUILD_TESTS OFF)
  FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(pugixml)
endif()

if(HYDROID_RES_BUILD_TESTS)
  if(HYDROID_RES_USE_SYSTEM_DEPS)
    find_package(GTest 1.15.2 EXACT REQUIRED CONFIG)
    find_package(nlohmann_json 3.11.3 EXACT REQUIRED CONFIG)
  else()
    # Keep gtest on the same CRT as the rest of the build on MSVC.
    set(gtest_force_shared_crt ON)
    set(INSTALL_GTEST OFF)
    set(BUILD_GMOCK OFF)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.15.2
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(googletest)

    # nlohmann/json: the golden tests read golden/data/*.json with it.
    FetchContent_Declare(nlohmann_json
      URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
      URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
  endif()
endif()

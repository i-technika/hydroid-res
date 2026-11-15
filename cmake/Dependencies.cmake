include(FetchContent)

# Every dependency is fetched at a pinned version, so what is installed on the machine never
# changes the build (#11). HYDROID_RES_USE_SYSTEM_DEPS uses installed packages instead, but only
# at exactly those versions, and fails rather than falling back to a fetch.
option(HYDROID_RES_USE_SYSTEM_DEPS
  "Use installed pugixml, expat, GoogleTest and nlohmann/json, at exactly the pinned versions" OFF)

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
  set(hydroid_res_fetched_pugixml OFF)
else()
  set(PUGIXML_NO_EXCEPTIONS OFF)
  set(PUGIXML_BUILD_TESTS OFF)
  FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE)
  # A parent project that made pugixml first decides its type, and FetchContent_MakeAvailable
  # would do nothing here, so only a pugixml made here is held static.
  FetchContent_GetProperties(pugixml)
  if(pugixml_POPULATED)
    set(hydroid_res_fetched_pugixml OFF)
  else()
    set(hydroid_res_fetched_pugixml ON)
    # Static whatever BUILD_SHARED_LIBS says, so a shared hydroid-res holds it and needs no
    # pugixml DLL next to it (#12). pugixml's option(BUILD_SHARED_LIBS) takes this plain variable
    # under CMP0077. Restoring with an unquoted value unsets it again if it was never set; if it
    # was only a cache entry, the plain variable left behind has the same value.
    set(hydroid_build_shared_libs ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(pugixml)
    set(BUILD_SHARED_LIBS ${hydroid_build_shared_libs})
    # Nor does a shared hydroid-res export the pugixml it holds, on Linux and macOS.
    set_target_properties(pugixml-static PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  endif()
endif()

# expat: aapt2's XML parser, which decides what is well-formed XML before pugixml reads it (#78).
if(HYDROID_RES_USE_SYSTEM_DEPS)
  find_package(expat 2.7.1 EXACT REQUIRED CONFIG)
  set(hydroid_res_fetched_expat OFF)
else()
  # Static whatever BUILD_SHARED_LIBS says, as pugixml (#12): expat has an option of its own for
  # that, and builds a static library with hidden visibility, so a shared hydroid-res does not
  # export it either.
  set(EXPAT_SHARED_LIBS OFF)
  set(EXPAT_BUILD_TOOLS OFF)
  set(EXPAT_BUILD_EXAMPLES OFF)
  set(EXPAT_BUILD_TESTS OFF)
  set(EXPAT_BUILD_DOCS OFF)
  set(EXPAT_BUILD_PKGCONFIG OFF)
  set(EXPAT_ENABLE_INSTALL OFF)
  FetchContent_Declare(expat
    URL      https://github.com/libexpat/libexpat/releases/download/R_2_7_1/expat-2.7.1.tar.xz
    URL_HASH SHA256=354552544b8f99012e5062f7d570ec77f14b412a3ff5c7d8d0dae62c0d217c30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_GetProperties(expat)
  if(expat_POPULATED)
    set(hydroid_res_fetched_expat OFF)
  else()
    set(hydroid_res_fetched_expat ON)
    FetchContent_MakeAvailable(expat)
  endif()
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

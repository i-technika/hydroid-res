include(FetchContent)

# pugixml: the only runtime dependency of the core (values, layout, drawable XML).
set(PUGIXML_NO_EXCEPTIONS OFF CACHE BOOL "" FORCE)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        v1.14
  GIT_SHALLOW    TRUE
  FIND_PACKAGE_ARGS 1.14)
FetchContent_MakeAvailable(pugixml)

if(HYDROID_RES_BUILD_TESTS)
  # Keep gtest on the same CRT as the rest of the build on MSVC.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES GTest)
  FetchContent_MakeAvailable(googletest)
endif()

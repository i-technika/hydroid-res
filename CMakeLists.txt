cmake_minimum_required(VERSION 3.24)

project(hydroid-res
  VERSION 0.1.0
  DESCRIPTION "Android resource system for Hydroid (UIKit, GTK4, WinUI 3)"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HYDROID_RES_BUILD_TESTS "Build hydroid-res unit and golden tests" ${PROJECT_IS_TOP_LEVEL})
option(HYDROID_RES_BUILD_TOOLS "Build the hydroid-rc resource compiler" ${PROJECT_IS_TOP_LEVEL})
option(HYDROID_RES_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(HYDROID_RES_REQUIRE_LOCALE_TESTS
  "Fail, rather than skip, tests that need a decimal-comma locale where none is installed" OFF)
option(HYDROID_RES_REQUIRE_NDK_TESTS
  "Fail, rather than leave out AConfiguration.MatchesNdk, where no NDK or Python is found" OFF)

# A shared build puts DLLs next to the executables that load them, as Windows has no rpath. Set
# before the dependencies so a shared GoogleTest lands there too.
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

# BUILD_SHARED_LIBS as hydroid-res was asked for it, as a plain variable or a cache entry, so
# CMake.LibraryTypes catches one that Dependencies.cmake failed to restore.
set(hydroid_res_shared_requested "${BUILD_SHARED_LIBS}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src)

if(HYDROID_RES_BUILD_TOOLS)
  add_subdirectory(tools/hydroid-rc)
endif()

if(HYDROID_RES_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

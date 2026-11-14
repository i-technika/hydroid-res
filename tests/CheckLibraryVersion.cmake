# Fails unless hydroid-res's VERSION is PROJECT_VERSION and its SOVERSION is the ABI version
# src/CMakeLists.txt promises: major.minor while the major is 0, as every 0.x minor release may
# break the ABI, and the major alone from 1.0 (#13). All three from tests/CMakeLists.txt.

if(NOT VERSION STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR "hydroid-res VERSION is '${VERSION}', not '${PROJECT_VERSION}'")
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" major_minor "${PROJECT_VERSION}")
if(CMAKE_MATCH_1 EQUAL 0)
  set(expected "${major_minor}")
else()
  set(expected "${CMAKE_MATCH_1}")
endif()
if(NOT SOVERSION STREQUAL expected)
  message(FATAL_ERROR
    "hydroid-res SOVERSION is '${SOVERSION}', not '${expected}' for ${PROJECT_VERSION}")
endif()
message(STATUS "hydroid-res: VERSION ${VERSION}, SOVERSION ${SOVERSION}")

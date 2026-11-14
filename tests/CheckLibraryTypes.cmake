# Fails unless hydroid-res is shared exactly when BUILD_SHARED_LIBS asks, and a fetched pugixml is
# static either way, so a shared hydroid-res holds it and has no pugixml DLL to ship (#12).
# SHARED is BUILD_SHARED_LIBS, HYDROID_RES_TYPE the target's TYPE, and PUGIXML_LINKS what
# pugixml::pugixml links, empty for installed dependencies; all from tests/CMakeLists.txt.

if(SHARED)
  set(expected SHARED_LIBRARY)
else()
  set(expected STATIC_LIBRARY)
endif()
if(NOT HYDROID_RES_TYPE STREQUAL expected)
  message(FATAL_ERROR "hydroid-res is a ${HYDROID_RES_TYPE}, but BUILD_SHARED_LIBS='${SHARED}'")
endif()

# An installed pugixml is whatever was installed.
if(NOT USE_SYSTEM_DEPS AND NOT PUGIXML_LINKS STREQUAL "pugixml-static")
  message(FATAL_ERROR "pugixml::pugixml links '${PUGIXML_LINKS}', not pugixml-static")
endif()
message(STATUS "hydroid-res: ${HYDROID_RES_TYPE}, pugixml::pugixml: ${PUGIXML_LINKS}")

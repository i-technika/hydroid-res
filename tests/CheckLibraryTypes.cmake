# Fails unless hydroid-res is shared exactly when BUILD_SHARED_LIBS asks, and a pugixml or expat
# that hydroid fetched is static either way, so a shared hydroid-res holds it and has no DLL of
# it to ship (#12). SHARED is BUILD_SHARED_LIBS, HYDROID_RES_TYPE the target's TYPE,
# FETCHED_PUGIXML whether Dependencies.cmake fetched pugixml itself, and PUGIXML_LINKS what
# pugixml::pugixml links then; FETCHED_EXPAT and EXPAT_TYPE the same for expat, whose target is
# the library itself; all from tests/CMakeLists.txt.

if(SHARED)
  set(expected SHARED_LIBRARY)
else()
  set(expected STATIC_LIBRARY)
endif()
if(NOT HYDROID_RES_TYPE STREQUAL expected)
  message(FATAL_ERROR "hydroid-res is a ${HYDROID_RES_TYPE}, but BUILD_SHARED_LIBS='${SHARED}'")
endif()

# An installed pugixml or expat, or one a parent project made first, is whatever it was made.
if(FETCHED_PUGIXML AND NOT PUGIXML_LINKS STREQUAL "pugixml-static")
  message(FATAL_ERROR "pugixml::pugixml links '${PUGIXML_LINKS}', not pugixml-static")
endif()
if(FETCHED_EXPAT AND NOT EXPAT_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "expat::expat is a '${EXPAT_TYPE}', not a STATIC_LIBRARY")
endif()
message(STATUS
  "hydroid-res: ${HYDROID_RES_TYPE}, pugixml::pugixml: ${PUGIXML_LINKS}, expat::expat: ${EXPAT_TYPE}")

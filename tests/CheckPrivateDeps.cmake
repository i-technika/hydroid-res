# Fails if a consumer of hydroid::res would get the include path of pugixml (#10) or expat (#78).
# CONSUMER_INCLUDES, PUGIXML_INCLUDES and EXPAT_INCLUDES are |-separated lists from
# tests/CMakeLists.txt.

string(REPLACE "|" ";" consumer "${CONSUMER_INCLUDES}")

foreach(dep IN ITEMS pugixml expat)
  string(TOUPPER "${dep}" upper)
  string(REPLACE "|" ";" dirs "${${upper}_INCLUDES}")
  # With nothing to look for, the loop below would pass vacuously.
  if(NOT dirs)
    message(FATAL_ERROR "${dep}::${dep} has no include directories, so there is nothing to check")
  endif()
  # list(FIND), unlike if(IN_LIST), needs no policy, and -P sets none.
  foreach(dir IN LISTS dirs)
    list(FIND consumer "${dir}" index)
    if(NOT index EQUAL -1)
      message(FATAL_ERROR "hydroid::res leaks ${dep}'s include path to its consumers: ${dir}")
    endif()
  endforeach()
endforeach()
message(STATUS "hydroid::res consumers get: ${consumer}")

# Fails if a consumer of hydroid::res would get pugixml's include path (#10).
# CONSUMER_INCLUDES and PUGIXML_INCLUDES are |-separated lists from tests/CMakeLists.txt.

string(REPLACE "|" ";" consumer "${CONSUMER_INCLUDES}")
string(REPLACE "|" ";" pugixml "${PUGIXML_INCLUDES}")

# With nothing to look for, the loop below would pass vacuously.
if(NOT pugixml)
  message(FATAL_ERROR "pugixml::pugixml has no include directories, so there is nothing to check")
endif()

# list(FIND), unlike if(IN_LIST), needs no policy, and -P sets none.
foreach(dir IN LISTS pugixml)
  list(FIND consumer "${dir}" index)
  if(NOT index EQUAL -1)
    message(FATAL_ERROR "hydroid::res leaks pugixml's include path to its consumers: ${dir}")
  endif()
endforeach()
message(STATUS "hydroid::res consumers get: ${consumer}")

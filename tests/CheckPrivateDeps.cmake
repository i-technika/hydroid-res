# Fails if a consumer of hydroid::res would get pugixml's include path (#10).
# CONSUMER_INCLUDES and PUGIXML_INCLUDES are |-separated lists from tests/CMakeLists.txt.
cmake_minimum_required(VERSION 3.24) # -P sets no policies; IN_LIST needs CMP0057.

string(REPLACE "|" ";" consumer "${CONSUMER_INCLUDES}")
string(REPLACE "|" ";" pugixml "${PUGIXML_INCLUDES}")

foreach(dir IN LISTS pugixml)
  if(dir IN_LIST consumer)
    message(FATAL_ERROR "hydroid::res leaks pugixml's include path to its consumers: ${dir}")
  endif()
endforeach()
message(STATUS "hydroid::res consumers get: ${consumer}")

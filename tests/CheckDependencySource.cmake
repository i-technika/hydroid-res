# Fails if a dependency did not come from where HYDROID_RES_USE_SYSTEM_DEPS says (#11): installed
# packages are imported targets, and fetched ones are built in this tree.
# USE_SYSTEM_DEPS is the option's value. DEPENDENCIES is a |-separated list of <target>=<IMPORTED>
# from tests/CMakeLists.txt.

string(REPLACE "|" ";" dependencies "${DEPENDENCIES}")

# With nothing to look at, the loop below would pass vacuously.
if(NOT dependencies)
  message(FATAL_ERROR "No dependencies were passed, so there is nothing to check")
endif()

foreach(entry IN LISTS dependencies)
  if(NOT entry MATCHES "^([^=]+)=(.*)$")
    message(FATAL_ERROR "Malformed entry: ${entry}")
  endif()
  set(target "${CMAKE_MATCH_1}")
  set(imported "${CMAKE_MATCH_2}")
  if(USE_SYSTEM_DEPS AND NOT imported)
    message(FATAL_ERROR "${target} was fetched, but HYDROID_RES_USE_SYSTEM_DEPS is ON")
  elseif(NOT USE_SYSTEM_DEPS AND imported)
    message(FATAL_ERROR "${target} is an installed package, but HYDROID_RES_USE_SYSTEM_DEPS is OFF")
  endif()
  message(STATUS "${target}: IMPORTED=${imported}")
endforeach()

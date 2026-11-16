# Fails unless hydroid-rc dumps a res/ folder whose path, and a package whose name, has characters
# that no ANSI code page of Windows here has: an accented Greek letter, which CP949 lacks, and Han,
# which CP1252 lacks. A narrow argv spells them ? (#87). RC is hydroid-rc and DIR a folder of the
# build tree to write in, both from tests/CMakeLists.txt.

set(res "${DIR}/Ελληνικά 漢字/res")
file(REMOVE_RECURSE "${DIR}")
file(WRITE "${res}/values/v.xml" "<resources><string name=\"s\">x</string></resources>\n")

# UTF8: on Windows, the output as hydroid-rc writes it, rather than read in the console's code page.
execute_process(COMMAND "${RC}" dump "${res}" "com.ελλάδα"
  RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err ENCODING UTF8)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "hydroid-rc dump failed (${result}):\n${err}")
endif()
foreach(expected IN ITEMS "Package name=com.ελλάδα id=7f" "resource 0x7f010000 string/s")
  string(FIND "${out}" "${expected}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "hydroid-rc dump does not print '${expected}':\n${out}")
  endif()
endforeach()
message(STATUS "hydroid-rc dumps ${res}")
file(REMOVE_RECURSE "${DIR}")

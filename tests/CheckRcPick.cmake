# Fails unless hydroid-rc pick prints what Android picked of the golden app's res/ under one
# condition of golden/data, 320dpi_ko-KR_night_land_sw600dp: the locale, and of every resource the
# id, and the text of a string. The qualifiers below are that condition's Configuration, as
# goldenDevice (GoldenUtil.h) cuts it, written as a folder name (#202). It also fails unless a
# device that names no version is of kEmulatedSdkVersion, and one that names one of that one,
# although parseConfig makes night night-v8. RC is hydroid-rc, RES the app's res/ and GOLDEN
# golden/data, all from tests/CMakeLists.txt.

set(package com.itechnika.hydroid.golden)

# Runs hydroid-rc pick with the arguments after OUT, and sets OUT to what it prints.
function(pick out)
  # UTF8: on Windows, the output as hydroid-rc writes it, rather than read in the console's code
  # page.
  execute_process(COMMAND "${RC}" pick "${RES}" ${ARGN} ${package}
    RESULT_VARIABLE result OUTPUT_VARIABLE text ERROR_VARIABLE err ENCODING UTF8)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "hydroid-rc pick ${ARGN} failed (${result}):\n${err}")
  endif()
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Fails unless TEXT has a line that starts with PREFIX and ends with SUFFIX.
function(expect_line text prefix suffix)
  string(FIND "${text}" "\n${prefix}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "hydroid-rc pick prints no line '${prefix}...':\n${text}")
  endif()
  math(EXPR at "${at} + 1")
  string(SUBSTRING "${text}" ${at} -1 line)
  string(FIND "${line}" "\n" end)
  string(SUBSTRING "${line}" 0 ${end} line)
  string(LENGTH "${line}" line_length)
  string(LENGTH "${suffix}" suffix_length)
  set(tail "")
  if(line_length GREATER_EQUAL suffix_length)
    math(EXPR from "${line_length} - ${suffix_length}")
    string(SUBSTRING "${line}" ${from} -1 tail)
  endif()
  if(NOT tail STREQUAL suffix)
    message(FATAL_ERROR "hydroid-rc pick prints '${line}', not one ending '${suffix}'")
  endif()
endfunction()

file(READ "${GOLDEN}/320dpi_ko-KR_night_land_sw600dp.json" golden)
string(JSON locales GET "${golden}" condition configuration locales)
if(NOT locales STREQUAL "ko-KR")
  message(FATAL_ERROR "the golden condition is of ${locales}, not ko-KR")
endif()
pick(out
  mcc310-mnc260-ko-rKR-ldltr-sw600dp-w1066dp-h600dp-normal-long-notround-nowidecg-lowdr-land-night-xhdpi-finger-keysexposed-qwerty-nonav-2400x1080
  --locales ko-KR)
set(out "\n${out}")
expect_line("${out}" "locale (ko-rKR)" "")

string(JSON values GET "${golden}" values)
string(JSON count LENGTH "${values}")
math(EXPR last "${count} - 1")
set(strings 0)
foreach(i RANGE ${last})
  string(JSON key MEMBER "${values}" ${i})
  string(JSON id GET "${values}" "${key}" id)
  string(JSON type GET "${values}" "${key}" type)
  set(suffix "")
  if(type EQUAL 3)  # TypedValue.TYPE_STRING
    string(JSON text GET "${values}" "${key}" string)
    set(suffix " \"${text}\"")
    math(EXPR strings "${strings} + 1")
  endif()
  expect_line("${out}" "resource ${id} ${key} (" "${suffix}")
endforeach()
string(REGEX MATCHALL "\nresource " lines "${out}")
list(LENGTH lines printed)
if(NOT printed EQUAL count)
  message(FATAL_ERROR "hydroid-rc pick prints ${printed} resources, golden/data has ${count}")
endif()

# values-v30/ and values-v37/ hold pick_version, and a device of API 37 takes values-v37/.
string(JSON version_id GET "${values}" string/pick_version id)
pick(out night)
expect_line("\n${out}" "resource ${version_id} string/pick_version (" " \"v37\"")
pick(out night-v30)
expect_line("\n${out}" "resource ${version_id} string/pick_version (" " \"v30\"")

message(STATUS "hydroid-rc pick prints the ${count} values, ${strings} strings, of golden/data")

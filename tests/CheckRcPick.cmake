# Fails unless hydroid-rc pick prints what Android picked of the golden app's res/ under three
# conditions of golden/data (#202): the locale it read them in, and of every resource the id and,
# but for a reference or a dimension or fraction, the value; of a pick_ string, which says the
# folder it is in, the configuration too. The qualifiers below are each condition's
# Configuration, as goldenDevice (GoldenUtil.h) cuts it, written as a folder name, but for the
# locale, which only --locales gives, as the condition asked Android for it. It also fails unless
# a device is of kEmulatedSdkVersion when its qualifiers name no version, and of the one they
# name, even below the one parseConfig raises them to (night-v4 is night-v8 as a folder), and
# that a <public> resource is PUBLIC. RC is hydroid-rc, RES the app's res/, GOLDEN golden/data
# and TABLE golden/table/res, all from tests/CMakeLists.txt.

set(package com.itechnika.hydroid.golden)

# Runs hydroid-rc pick with the arguments after OUT, and sets OUT to what it prints, after a
# newline, so that every line starts with one.
function(pick out)
  # UTF8: on Windows, the output as hydroid-rc writes it, rather than read in the console's code
  # page.
  execute_process(COMMAND "${RC}" pick "${RES}" ${ARGN} ${package}
    RESULT_VARIABLE result OUTPUT_VARIABLE text ERROR_VARIABLE err ENCODING UTF8)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "hydroid-rc pick ${ARGN} failed (${result}):\n${err}")
  endif()
  set(${out} "\n${text}" PARENT_SCOPE)
endfunction()

# Sets OUT to the line of TEXT that starts with PREFIX, or fails.
function(line_of text prefix out)
  string(FIND "${text}" "\n${prefix}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "hydroid-rc pick prints no line '${prefix}...':\n${text}")
  endif()
  math(EXPR at "${at} + 1")
  string(SUBSTRING "${text}" ${at} -1 line)
  string(FIND "${line}" "\n" end)
  string(SUBSTRING "${line}" 0 ${end} line)
  set(${out} "${line}" PARENT_SCOPE)
endfunction()

# Sets OUT to "#aarrggbb" of a color as TypedValue.data holds it, a signed 32-bit number.
function(color_of data out)
  math(EXPR hex "${data} & 0xffffffff" OUTPUT_FORMAT HEXADECIMAL)
  string(SUBSTRING "${hex}" 2 -1 hex)
  string(TOLOWER "${hex}" hex)
  string(LENGTH "${hex}" length)
  while(length LESS 8)
    string(PREPEND hex "0")
    math(EXPR length "${length} + 1")
  endwhile()
  set(${out} "#${hex}" PARENT_SCOPE)
endfunction()

# Fails unless pick, with the arguments after LOCALE, prints what golden/data's NAME.json holds,
# and the locale LOCALE.
function(check_golden name locale)
  file(READ "${GOLDEN}/${name}.json" golden)
  pick(out ${ARGN})
  line_of("${out}" "locale " line)
  if(NOT line STREQUAL "locale (${locale})")
    message(FATAL_ERROR "${name}: hydroid-rc pick prints '${line}', not locale (${locale})")
  endif()

  string(JSON values GET "${golden}" values)
  string(JSON count LENGTH "${values}")
  math(EXPR last "${count} - 1")
  set(compared 0)
  foreach(i RANGE ${last})
    string(JSON key MEMBER "${values}" ${i})
    string(JSON id GET "${values}" "${key}" id)
    string(JSON type GET "${values}" "${key}" type)
    string(JSON data GET "${values}" "${key}" data)
    set(prefix "resource ${id} ${key} ")
    line_of("${out}" "${prefix}" line)
    string(LENGTH "${prefix}" from)
    string(SUBSTRING "${line}" ${from} -1 rest)  # PUBLIC, "(", the configuration, ") ", the value
    string(REGEX REPLACE "^(PUBLIC )?\\(" "" rest "${rest}")
    string(FIND "${rest}" ") " close)
    string(SUBSTRING "${rest}" 0 ${close} config)
    math(EXPR close "${close} + 2")
    string(SUBSTRING "${rest}" ${close} -1 value)

    string(SUBSTRING "${value}" 0 1 first)
    if(first STREQUAL "@")
      continue()  # a reference, which golden/data has resolved
    elseif(type EQUAL 3)  # TypedValue.TYPE_STRING
      string(JSON text GET "${values}" "${key}" string)
      set(expected "\"${text}\"")
      if(key MATCHES "^string/pick_")
        # values/ says "default", and values-<qualifiers>/ the qualifiers, which aapt2 may give
        # a version.
        set(want "${text}")
        if(text STREQUAL "default")
          set(want "")
        endif()
        string(REGEX REPLACE "-?v[0-9]+$" "" folder "${config}")
        if(NOT folder STREQUAL want AND NOT config STREQUAL want)
          message(FATAL_ERROR "${name}: hydroid-rc pick prints '${line}', from another folder "
            "than '${text}'")
        endif()
      endif()
    elseif(type EQUAL 16)  # TYPE_INT_DEC
      set(expected "${data}")
    elseif(type EQUAL 18)  # TYPE_INT_BOOLEAN
      if(data EQUAL 0)
        set(expected false)
      else()
        set(expected true)
      endif()
    elseif(type GREATER_EQUAL 28 AND type LESS_EQUAL 31)  # TYPE_INT_COLOR_*
      color_of(${data} expected)
    else()
      continue()  # a dimension or fraction, which pick prints as aapt2 rounds it
    endif()
    if(NOT value STREQUAL expected)
      message(FATAL_ERROR "${name}: hydroid-rc pick prints '${line}', not the value ${expected}")
    endif()
    math(EXPR compared "${compared} + 1")
  endforeach()
  string(REGEX MATCHALL "\nresource " lines "${out}")
  list(LENGTH lines printed)
  if(NOT printed EQUAL count)
    message(FATAL_ERROR "${name}: hydroid-rc pick prints ${printed} resources, golden/data has "
      "${count}")
  endif()
  message(STATUS "${name}: the ${count} ids and ${compared} values of golden/data")
endfunction()

# Fails unless pick, with the arguments after EXPECTED, prints the line of the string NAME as
# ending with EXPECTED, its configuration and value.
function(expect_string name expected)
  pick(out ${ARGN})
  line_of("${out}" "resource " ignored)  # that it printed resources at all
  string(REGEX MATCH "\nresource [^ ]+ string/${name} [^\n]*" line "${out}")
  string(LENGTH "${line}" line_length)
  string(LENGTH "${expected}" expected_length)
  set(tail "")
  if(line_length GREATER expected_length)
    math(EXPR from "${line_length} - ${expected_length}")
    string(SUBSTRING "${line}" ${from} -1 tail)
  endif()
  if(NOT tail STREQUAL expected)
    message(FATAL_ERROR "${ARGN}: hydroid-rc pick prints '${line}', not one ending '${expected}'")
  endif()
endfunction()

# One language, which the app has values for.
check_golden(320dpi_ko-KR_night_land_sw600dp ko-rKR
  mcc310-mnc260-ldltr-sw600dp-w1066dp-h600dp-normal-long-notround-nowidecg-lowdr-land-night-xhdpi-finger-keysexposed-qwerty-nonav-2400x1080
  --locales ko-KR)
# Two, of which the app has values only for the second.
check_golden(locales_ja-JP+ko-KR ko-rKR
  mcc310-mnc260-ldltr-sw320dp-w320dp-h568dp-normal-long-notround-nowidecg-lowdr-port-notnight-xhdpi-finger-keysexposed-qwerty-nonav-2400x1080
  --locales ja-JP,ko-KR)
# Two that the app has values for, of which the first is the one.
check_golden(locales_en-US+ko-KR en-rUS
  mcc310-mnc260-ldltr-sw320dp-w320dp-h568dp-normal-long-notround-nowidecg-lowdr-port-notnight-xhdpi-finger-keysexposed-qwerty-nonav-2400x1080
  --locales en-US,ko-KR)

# A resource that is <public> is PUBLIC, as dump prints it.
execute_process(COMMAND "${RC}" pick "${TABLE}" ""
  RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err ENCODING UTF8)
if(NOT result EQUAL 0 OR NOT out MATCHES "\nresource [^ ]+ string/app_name PUBLIC \\(\\) \"")
  message(FATAL_ERROR "hydroid-rc pick of ${TABLE} (${result}) prints no PUBLIC app_name:\n"
    "${out}${err}")
endif()

# values-v21/, values-v30/, values-v37/ and values-v38/ hold pick_version, and values-night/,
# which aapt2 makes night-v8, pick_night.
expect_string(pick_version " (v37) \"v37\"" night)
expect_string(pick_version " (v30) \"v30\"" night-v30)
expect_string(pick_version " () \"default\"" night-v4)
expect_string(pick_night " () \"default\"" night-v4)
expect_string(pick_night " (night-v8) \"night\"" night-v8)
# anydpi-v4.1 is a folder, anydpi-v21, but v4.1 no version of a device.
execute_process(COMMAND "${RC}" pick "${RES}" anydpi-v4.1 ${package}
  RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT result EQUAL 1 OR NOT err MATCHES "not the version of a device: v4\\.1")
  message(FATAL_ERROR "hydroid-rc pick anydpi-v4.1 ends with ${result}, not an error:\n${err}")
endif()

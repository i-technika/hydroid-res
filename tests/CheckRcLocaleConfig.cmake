# Fails unless hydroid-rc pick, given the app's LocaleConfig, prints what Android picked of
# golden/localeconfig's res/ through each lifecycle of golden/localeconfig/data (#268): its steps
# one after another on the same Resources, the first as the qualifiers and --locales, each after
# it by --then, and for each step the locale the configuration has first and the value of every
# string, which says the folder it is in. The qualifiers of a step are only the mcc, mnc and
# orientation of its Configuration, as the app's values have no other qualifier than a language
# and an mcc. It also fails unless the LocaleConfig is what makes the second step of "fr+es" read
# es too, unless a --then of no --locales keeps the languages of the device just before it, and
# unless pick ends with an error for a LocaleConfig it cannot read or one given twice.
# RC is hydroid-rc, RES the app's res/ and GOLDEN golden/localeconfig/data, all from
# tests/CMakeLists.txt.

set(package com.itechnika.hydroid.golden.localeconfig)
set(locale_config "${RES}/xml/locales_config.xml")

# Runs hydroid-rc pick with the arguments after OUT, and sets OUT to what it prints, after a
# newline, so that every line starts with one.
function(pick out)
  execute_process(COMMAND "${RC}" pick "${RES}" ${ARGN} ${package}
    RESULT_VARIABLE result OUTPUT_VARIABLE text ERROR_VARIABLE err ENCODING UTF8)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "hydroid-rc pick ${ARGN} failed (${result}):\n${err}")
  endif()
  set(${out} "\n${text}" PARENT_SCOPE)
endfunction()

# Sets OUT to the lines of TEXT, as pick sets it, that the device numbered INDEX, from 0, printed:
# those from its locale line up to the next, after a newline.
function(block_of text index out)
  set(rest "${text}")
  foreach(i RANGE ${index})
    string(FIND "${rest}" "\nlocale " at)
    if(at EQUAL -1)
      message(FATAL_ERROR "hydroid-rc pick prints no lines for device ${i}:\n${text}")
    endif()
    math(EXPR at "${at} + 1")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endforeach()
  string(FIND "${rest}" "\nlocale " end)
  string(SUBSTRING "${rest}" 0 ${end} block)
  set(${out} "\n${block}" PARENT_SCOPE)
endfunction()

# Sets OUT to what follows "resource <id> string/NAME " on its line of BLOCK, or fails.
function(string_of block name out)
  string(REGEX MATCH "\nresource [^ ]+ string/${name} [^\n]*" line "${block}")
  if(line STREQUAL "")
    message(FATAL_ERROR "hydroid-rc pick prints no string/${name}:\n${block}")
  endif()
  string(REGEX REPLACE "^\nresource [^ ]+ string/${name} " "" rest "${line}")
  set(${out} "${rest}" PARENT_SCOPE)
endfunction()

file(GLOB lifecycles "${GOLDEN}/*.json")
list(LENGTH lifecycles files)
if(files EQUAL 0)
  message(FATAL_ERROR "no lifecycle in ${GOLDEN}")
endif()
foreach(file IN LISTS lifecycles)
  file(READ "${file}" golden)
  string(JSON name GET "${golden}" name)
  string(JSON steps GET "${golden}" steps)
  string(JSON count LENGTH "${steps}")
  math(EXPR last "${count} - 1")

  set(args "")
  foreach(i RANGE ${last})
    string(JSON condition GET "${steps}" ${i} condition)
    string(JSON requested GET "${condition}" requestedLocales)
    string(JSON mcc GET "${condition}" configuration mcc)
    string(JSON mnc GET "${condition}" configuration mnc)
    string(JSON orientation GET "${condition}" configuration orientation)
    if(orientation EQUAL 2)  # Configuration.ORIENTATION_LANDSCAPE
      set(qualifiers "mcc${mcc}-mnc${mnc}-land")
    else()
      set(qualifiers "mcc${mcc}-mnc${mnc}-port")
    endif()
    if(i GREATER 0)
      list(APPEND args --then)
    endif()
    list(APPEND args "${qualifiers}" --locales "${requested}")
  endforeach()
  # The app has its LocaleConfig, which pick gives as Android does: so for a lifecycle whose first
  # step has one language, which Android was not given it for either ("fr then fr+es"). Not for
  # the app's own Resources given languages by LocaleManager ("app ..."), whose first step has
  # more, but which the golden says Android was not given it for.
  string(JSON given GET "${golden}" localeConfig)
  string(JSON first_requested GET "${steps}" 0 condition requestedLocales)
  if(given OR NOT first_requested MATCHES ",")
    list(APPEND args --locale-config "${locale_config}")
  endif()
  pick(out ${args})

  string(REGEX MATCHALL "\nlocale " locale_lines "${out}")
  list(LENGTH locale_lines printed)
  if(NOT printed EQUAL count)
    message(FATAL_ERROR "${name}: hydroid-rc pick prints ${printed} locales, not one for each of "
      "the ${count} steps:\n${out}")
  endif()
  set(compared 0)
  foreach(i RANGE ${last})
    string(JSON step GET "${steps}" ${i} step)
    string(JSON locales GET "${steps}" ${i} condition configuration locales)
    string(JSON values GET "${steps}" ${i} values)
    block_of("${out}" ${i} block)

    # The first of the configuration's locales, fr-FR, as a folder names it, fr-rFR.
    string(REGEX REPLACE ",.*" "" first "${locales}")
    string(REGEX REPLACE "^([a-z]+)-([A-Z][A-Z])$" "\\1-r\\2" first "${first}")
    string(REGEX MATCH "^\nlocale \\([^\n]*\\)" line "${block}")
    if(NOT line STREQUAL "\nlocale (${first})")
      message(FATAL_ERROR "${name}, ${step}: hydroid-rc pick prints '${line}', not "
        "locale (${first})")
    endif()

    string(REGEX MATCHALL "\nresource [^ ]+ string/" string_lines "${block}")
    list(LENGTH string_lines strings)
    string(JSON value_count LENGTH "${values}")
    if(NOT strings EQUAL value_count)
      message(FATAL_ERROR "${name}, ${step}: hydroid-rc pick prints ${strings} strings, the "
        "golden ${value_count}")
    endif()
    math(EXPR value_last "${value_count} - 1")
    foreach(v RANGE ${value_last})
      string(JSON key MEMBER "${values}" ${v})
      string(REGEX REPLACE "^string/" "" string_name "${key}")
      string(JSON kind TYPE "${values}" "${key}" string)
      if(kind STREQUAL "NULL")
        set(expected "no value")
      else()
        string(JSON text GET "${values}" "${key}" string)
        # Each string says the folder it is in, and values/ "default".
        set(folder "${text}")
        if(text STREQUAL "default" OR string_name STREQUAL "app_name")
          set(folder "")
        endif()
        set(expected "(${folder}) \"${text}\"")
      endif()
      string_of("${block}" "${string_name}" got)
      if(NOT got STREQUAL expected)
        message(FATAL_ERROR "${name}, ${step}: hydroid-rc pick prints string/${string_name} "
          "${got}, not ${expected}")
      endif()
      math(EXPR compared "${compared} + 1")
    endforeach()
  endforeach()
  message(STATUS "${name}: the ${count} steps, ${compared} values")
endforeach()

# Without the LocaleConfig, the turned device of fr and es reads in fr alone, so string/es, of
# values-es/ only, is that of values/; with it, that of values-es/, as the golden has it.
pick(out mcc310-mnc260-port --locales fr-FR,es-ES --then mcc310-mnc260-land)
block_of("${out}" 1 block)
string_of("${block}" es got)
if(NOT got STREQUAL "() \"default\"")
  message(FATAL_ERROR "without --locale-config, hydroid-rc pick prints string/es ${got} after "
    "the turn, not () \"default\"")
endif()
pick(out mcc310-mnc260-port --locales fr-FR,es-ES --then mcc310-mnc260-land
  --locale-config "${locale_config}")
block_of("${out}" 1 block)
string_of("${block}" es got)
if(NOT got STREQUAL "(es) \"es\"")
  message(FATAL_ERROR "with --locale-config, and a --then of no --locales, hydroid-rc pick "
    "prints string/es ${got} after the turn, not (es) \"es\"")
endif()

# A --then of no --locales has the languages of the device just before it, not of the first.
function(expect_locale text index locale)
  block_of("${text}" ${index} block)
  string(REGEX MATCH "^\nlocale \\([^\n]*\\)" line "${block}")
  if(NOT line STREQUAL "\nlocale (${locale})")
    message(FATAL_ERROR "device ${index} (from 0) has '${line}', not locale (${locale}):\n${text}")
  endif()
endfunction()
pick(out port --locales fr-FR,es-ES --then land --locales es-ES,fr-FR --then port)
expect_locale("${out}" 2 es-rES)
# And, after one of a locale in its qualifiers and no --locales, that locale, unless its own
# qualifiers name a language.
pick(out fr-rFR-port --then land --then es-land)
expect_locale("${out}" 1 fr-rFR)
expect_locale("${out}" 2 es)

# A LocaleConfig that cannot be read is an error, and one given twice a misuse.
execute_process(COMMAND "${RC}" pick "${RES}" port --locale-config "${RES}/xml/no-such.xml"
  RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT result EQUAL 1 OR NOT err MATCHES "error: .*no-such\\.xml")
  message(FATAL_ERROR "hydroid-rc pick of a missing LocaleConfig ends with ${result}, not an "
    "error:\n${err}")
endif()
execute_process(COMMAND "${RC}" pick "${RES}" port --locale-config "${locale_config}"
  --locale-config "${locale_config}"
  RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT result EQUAL 2 OR NOT err MATCHES "usage:")
  message(FATAL_ERROR "hydroid-rc pick of two --locale-config ends with ${result}, not usage:\n"
    "${err}")
endif()

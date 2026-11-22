/* Stable C API over hydroid-res, used by the Swift (UIKit) adapter.
 *
 * What v0.1 gives: a table loaded from res/ folders, the configuration of a device, and the value
 * of a resource for it, as the C++ API has them (hydroid/res/ResourceTable.h, Resources.h). A
 * value is given as it is in the table: a reference is not followed (step 6), nor are the items of
 * a style, array or plurals reachable yet. The API is not fixed before step 10, and may change.
 *
 * Text is UTF-8 everywhere, paths too. No function throws or stops the program on what it is
 * given: one that fails says so by what it returns. Every pointer argument must be valid, but
 * where it says NULL is allowed. */
#ifndef HYDROID_RES_H
#define HYDROID_RES_H

#include <stddef.h>
#include <stdint.h>

#include "hydroid/res/Export.h"
#include "hydroid/res/VersionNumber.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the library that was built, "major.minor.patch". The string is static.
 * HYDROID_RES_VERSION is the version of these headers. */
HYDROID_RES_API const char* hydroid_res_version(void);

/* ---- The table ----
 *
 * As ResourceTable: the packages loaded from res/ folders, with the ids aapt2 gives them. It may
 * not change, by hydroid_res_table_add_package, while another thread uses it or a
 * hydroid_res_resources of it. */
typedef struct hydroid_res_table hydroid_res_table;

/* An empty table, or NULL if there is no memory for one. */
HYDROID_RES_API hydroid_res_table* hydroid_res_table_create(void);
/* Frees `table`, which no hydroid_res_resources may still use. NULL does nothing. */
HYDROID_RES_API void hydroid_res_table_destroy(hydroid_res_table* table);

/* Loads the res/ folder at `res` as the package `name` with the id `id` (0x7f for an app), after
 * those already loaded, as ResourceTable::addPackage does. 1 if it loaded; 0 if not, and the table
 * holds what it held, with the reasons in hydroid_res_table_error. Either way, the strings read
 * from the table before are gone. For now, where memory runs out: as it lists the folder, the
 * program ends with libstdc++ (GCC's library, #262); in pugixml, what the files hold may be left
 * out, or failed for the wrong reason (#263); and the table may be left other than it was
 * (#257). */
HYDROID_RES_API int hydroid_res_table_add_package(hydroid_res_table* table, const char* res,
                                                  const char* name, uint8_t id);

/* Why the last hydroid_res_table_add_package failed: one problem a line, each ended with '\n', as
 * "file:line: message", or "file: message" or "message" where it has no line or no file. "" if it
 * loaded, or none was called. The string is the table's, and stays until the next
 * hydroid_res_table_add_package. */
HYDROID_RES_API const char* hydroid_res_table_error(const hydroid_res_table* table);

/* The id of the resource `name`, "com.example:string/app_name", "@com.example:string/app_name",
 * or "@string/app_name" in `default_package`, which may be NULL for none; 0 for no resource, as
 * no resource has the id 0. */
HYDROID_RES_API uint32_t hydroid_res_table_find(const hydroid_res_table* table, const char* name,
                                                const char* default_package);

/* ---- The configuration of a device ----
 *
 * The fields of Config (hydroid/res/Config.h), with its numbers, which are those of Android's
 * AConfiguration (android/configuration.h): orientation 1 is ACONFIGURATION_ORIENTATION_PORT, and
 * so on. 0 is "any", so a hydroid_res_config of all 0 is the default configuration. The locale
 * fields are NUL-padded, and not NUL-terminated where they are full: "ko" and "KR", or "sr" and
 * "Latn", and a variant of 8 characters, "basiceng". */
typedef struct hydroid_res_config {
  uint16_t mcc;
  uint16_t mnc;
  char lang[4];
  char region[4];
  char script[4];
  char variant[8];
  uint8_t grammatical_gender;
  uint8_t layout_dir;
  uint16_t smallest_width_dp;
  uint16_t width_dp;
  uint16_t height_dp;
  uint8_t screen_size;
  uint8_t screen_long;
  uint8_t screen_round;
  uint8_t wide_color_gamut;
  uint8_t hdr;
  uint8_t orientation;
  uint8_t ui_mode_type;
  uint8_t ui_mode_night;
  uint16_t density; /* dpi; 0xfffe is anydpi, 0xffff nodpi */
  uint8_t touchscreen;
  uint8_t keys_hidden;
  uint8_t keyboard;
  uint8_t nav_hidden;
  uint8_t navigation;
  uint16_t screen_width; /* the screen in pixels */
  uint16_t screen_height;
  uint16_t sdk_version;
  uint16_t minor_version;
} hydroid_res_config;

/* ---- Values ---- */

/* The kinds of ValueKind (hydroid/res/Value.h), in its order. */
typedef enum hydroid_res_value_kind {
  HYDROID_RES_VALUE_NULL = 0,
  HYDROID_RES_VALUE_EMPTY,
  HYDROID_RES_VALUE_ID,
  HYDROID_RES_VALUE_REFERENCE,
  HYDROID_RES_VALUE_ATTR_REF,
  HYDROID_RES_VALUE_STRING,
  HYDROID_RES_VALUE_STYLED_STRING,
  HYDROID_RES_VALUE_COLOR,
  HYDROID_RES_VALUE_DIMENSION,
  HYDROID_RES_VALUE_FRACTION,
  HYDROID_RES_VALUE_FLOAT,
  HYDROID_RES_VALUE_INT,
  HYDROID_RES_VALUE_BOOL,
  HYDROID_RES_VALUE_FILE,
  HYDROID_RES_VALUE_STYLE,
  HYDROID_RES_VALUE_ARRAY,
  HYDROID_RES_VALUE_PLURALS,
  HYDROID_RES_VALUE_ATTR_DECL,
  HYDROID_RES_VALUE_STYLEABLE_DECL
} hydroid_res_value_kind;

/* One value of a resource. By its kind:
 *   - REFERENCE, ATTR_REF: `data` is the id it names, which is not followed.
 *   - COLOR: `data` is 0xAARRGGBB, `format` how it was written: 28 #AARRGGBB, 29 #RRGGBB, 30 #ARGB,
 *     31 #RGB, as Android's TypedValue.TYPE_INT_COLOR_*.
 *   - INT: `data` is the bits of the int32_t, `format` 16 if it was written in decimal, 17 in hex.
 *   - BOOL: `data` is 1 or 0.
 *   - FLOAT: `data` is the bits of the float.
 *   - DIMENSION, FRACTION: `data` is Android's complex data, with its unit (Complex.h).
 *   - STRING, STYLED_STRING: `string` is the text, `string_length` bytes long, which can hold a
 *     NUL before the one that ends it. A styled string's spans are not given.
 *   - FILE: `string` is its path in the APK, "res/drawable-hdpi-v4/icon.png", which is not where
 *     it is in the res/ folder, and `format` what aapt2 makes of it: 0 kept as it is, 1 compiled
 *     XML, 2 a PNG.
 * `data` and `format` are 0 for the other kinds, and `string` NULL. A string is the table's, and
 * stays until the table changes (hydroid_res_table_add_package) or goes. */
typedef struct hydroid_res_value {
  hydroid_res_value_kind kind;
  uint32_t data;
  uint8_t format;
  const char* string;
  size_t string_length;
} hydroid_res_value;

/* ---- A table for one device ----
 *
 * As Resources: the values of a table that the configuration of the device picks. Every function
 * may be called from any thread at any time, but not while the table changes. */
typedef struct hydroid_res_resources hydroid_res_resources;

/* For `table`, which must outlive it, and the default configuration; NULL if there is no memory
 * for it. */
HYDROID_RES_API hydroid_res_resources* hydroid_res_resources_create(const hydroid_res_table* table);
/* NULL does nothing. */
HYDROID_RES_API void hydroid_res_resources_destroy(hydroid_res_resources* resources);

/* Sets the state of the device, as Resources::setConfiguration does: `device`, and the user's
 * languages, the most wanted first, as BCP 47 language tags ("ko-KR", "sr-Latn-RS"), of which the
 * one the table has resources for replaces the locale of `device`. `locales` may be NULL when
 * `locale_count` is 0. 1 if it did; 0 if a tag is not a language tag (parseLanguageTag), or there
 * was no memory for it, and nothing has changed. */
HYDROID_RES_API int hydroid_res_resources_set_configuration(hydroid_res_resources* resources,
                                                            const hydroid_res_config* device,
                                                            const char* const* locales,
                                                            size_t locale_count);

/* The configuration the values are picked for, into `out`: the device's as
 * hydroid_res_resources_set_configuration made it, with the locale it chose. 1 if it did; 0 if
 * there was no memory to take in what the table has changed, which the first call after a change
 * does, and `out` is left as it was. */
HYDROID_RES_API int hydroid_res_resources_configuration(const hydroid_res_resources* resources,
                                                        hydroid_res_config* out);

/* A number that changes whenever the values may (Resources::generation), into `out`. 1 if it did;
 * 0 as for hydroid_res_resources_configuration, and `out` is left as it was. */
HYDROID_RES_API int hydroid_res_resources_generation(const hydroid_res_resources* resources,
                                                     uint32_t* out);

/* The value of the resource `id` for the configuration, into `out`. 1 if it has one; 0 if the
 * table has no such resource or none of its values matches, and `out` is a NULL value; -1 if there
 * was no memory to look, and `out` is left as it was. `generation`, if not NULL, is set to the
 * generation the answer is of, but on -1. */
HYDROID_RES_API int hydroid_res_resources_value(const hydroid_res_resources* resources, uint32_t id,
                                                hydroid_res_value* out, uint32_t* generation);

/* As hydroid_res_resources_value, for the configuration with the density `density` (dpi, 0 for the
 * device's own), as Resources::candidateForDensity. */
HYDROID_RES_API int hydroid_res_resources_value_for_density(const hydroid_res_resources* resources,
                                                            uint32_t id, uint16_t density,
                                                            hydroid_res_value* out,
                                                            uint32_t* generation);

#ifdef __cplusplus
}
#endif

#endif /* HYDROID_RES_H */

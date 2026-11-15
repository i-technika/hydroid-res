#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "hydroid/res/Export.h"
#include "hydroid/res/Value.h"

// Literal value parsers for values XML text and XML attributes. Each follows aapt2: leading and
// trailing ASCII whitespace is ignored, anything else that aapt2 rejects returns nullopt.
// golden/aapt2/values.tsv holds what aapt2 itself writes for some 4400 texts, and
// Aapt2ValueTest checks these parsers against it.
//
// On purpose, parseFloatValue (and so parseLiteral) rejects some text that aapt2 accepts. aapt2
// hands the text to strtof, so it also takes whatever else strtof reads, as long as the text
// starts with a digit, '.', '-' or '+':
//   - Infinity and NaN: "-inf", "+nan", "-infinity" (aapt2 writes a float inf or NaN, or for
//     "-infdp" an undefined dimension). Unsigned "inf" and "nan" fail aapt2's first check too.
//     The bits of a NaN come from the host's strtof: "-nan(1)" is 0xff800001 on Windows and
//     0xffc00001 on Linux and macOS.
//   - Hex floats: "0x10px" (16px), "-0x1p4", "0X10". "0x10" under kFormatInteger is still an
//     integer, since that parser runs first.
//   - Numbers past FLT_MAX: "1e39" (aapt2 writes inf).
//   - Dimensions and fractions of magnitude 2^41 or more, "3e12dp": aapt2's 64-bit conversion is
//     undefined there. Its x86 builds write 0 with the unit. Its arm64 build for macOS keeps the
//     unit but writes the mantissa 0xffffff for a positive one ("1e39dp", "3e12dp" and
//     "2199023255552dp" are 0xffffff01, "1e39%" and "1e20px" 0xffffff00) and 0x000001 for a
//     negative dimension ("-1e39dp", "-3e12dp" and "-infdp" are 0x00000101).
// None of these is a value a resource means to hold, and a resource compiler is better off
// reporting them than storing inf or a platform-dependent dimension.

namespace hydroid::res {

// Formats an item may take; same bits as Android's ResTable_map and attrs.xml format="".
enum FormatBits : uint32_t {
  kFormatReference = 1u << 0,
  kFormatString = 1u << 1,
  kFormatInteger = 1u << 2,
  kFormatBoolean = 1u << 3,
  kFormatColor = 1u << 4,
  kFormatFloat = 1u << 5,
  kFormatDimension = 1u << 6,
  kFormatFraction = 1u << 7,
  kFormatAny = 0x0000ffffu,
  kFormatEnum = 1u << 16,
  kFormatFlags = 1u << 17,
};

// #RGB, #ARGB, #RRGGBB, #AARRGGBB -> Color: 0xAARRGGBB plus which of the four was written
// (aapt2 keeps it as the Res_value type, TYPE_INT_COLOR_RGB4 ... ARGB8).
HYDROID_RES_API std::optional<Value> parseColor(std::string_view s);

// true/True/TRUE, false/False/FALSE -> Bool.
HYDROID_RES_API std::optional<Value> parseBool(std::string_view s);

// Decimal int32 ("-12") or hex uint32 ("0x7f010000", no sign, lowercase x) -> Int.
HYDROID_RES_API std::optional<Value> parseInteger(std::string_view s);

// "1.5" -> Float, "16dp" -> Dimension, "50%" / "50%p" -> Fraction. Units are case-sensitive and
// must follow the number directly ("16 dp" is rejected); "dip" is an alias of "dp". The number is
// rounded to the nearest float like strtof; dimensions and fractions then keep aapt2's complex
// encoding (see Complex.h), so "0.1dp" reads back as 0.100000024. Text longer than 126
// characters after trimming is rejected, as in aapt2. See the top of this file for the inputs
// aapt2 takes and this rejects.
HYDROID_RES_API std::optional<Value> parseFloatValue(std::string_view s);

// Tries the literal parsers allowed by `formats` in aapt2's order: color, bool, integer, then
// float/dimension/fraction. When `formats` allows more than float/dimension/fraction, a plain
// float that is 1 or more off the same text read as a double is rejected, as aapt2 keeps such
// text as a string: "9638178525" and "-2147483649" under kFormatAny, but not "2147483648", which
// a float holds exactly. References and strings are handled by the values parser (step 4).
HYDROID_RES_API std::optional<Value> parseLiteral(std::string_view s, uint32_t formats);

}  // namespace hydroid::res

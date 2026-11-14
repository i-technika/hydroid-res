#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "hydroid/res/Value.h"

// Literal value parsers for values XML text and XML attributes. Each follows aapt2: leading and
// trailing ASCII whitespace is ignored, anything else that aapt2 rejects returns nullopt.

namespace hydroid::res {

// Formats an item may take; same bits as Android's ResTable_map and attrs.xml format="".
enum FormatBits : uint32_t {
  kFormatReference = 1u << 0,
  kFormatString    = 1u << 1,
  kFormatInteger   = 1u << 2,
  kFormatBoolean   = 1u << 3,
  kFormatColor     = 1u << 4,
  kFormatFloat     = 1u << 5,
  kFormatDimension = 1u << 6,
  kFormatFraction  = 1u << 7,
  kFormatAny       = 0x0000ffffu,
  kFormatEnum      = 1u << 16,
  kFormatFlags     = 1u << 17,
};

// #RGB, #ARGB, #RRGGBB, #AARRGGBB → Color (always stored as 0xAARRGGBB).
std::optional<Value> parseColor(std::string_view s);

// true/True/TRUE, false/False/FALSE → Bool.
std::optional<Value> parseBool(std::string_view s);

// Decimal int32 ("-12") or hex uint32 ("0x7f010000", no sign, lowercase x) → Int.
std::optional<Value> parseInteger(std::string_view s);

// "1.5" → Float, "16dp" → Dimension, "50%" / "50%p" → Fraction. Units are case-sensitive and
// must follow the number directly ("16 dp" is rejected); "dip" is an alias of "dp". Dimensions and
// fractions keep aapt2's complex encoding (see Complex.h), so "0.1dp" reads back as 0.100000024.
std::optional<Value> parseFloatValue(std::string_view s);

// Tries the literal parsers allowed by `formats` in aapt2's order: color, bool, integer, then
// float/dimension/fraction. References and strings are handled by the values parser (step 4).
std::optional<Value> parseLiteral(std::string_view s, uint32_t formats);

}  // namespace hydroid::res

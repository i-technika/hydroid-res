#include "hydroid/res/ValueParser.h"

#include "hydroid/res/Complex.h"

#include <cmath>
#include <cstdint>
#include <locale>
#include <sstream>
#include <string>

namespace hydroid::res {
namespace {

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
  return s;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Length of the leading number in `s`: [+-]? (digits [. digits*] | . digits) ([eE][+-]?digits)?
// The same text strtof() would consume, minus inf/nan/hex floats. 0 if there is no number.
size_t scanNumber(std::string_view s) {
  size_t i = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
  size_t digits = 0;
  while (i < s.size() && isDigit(s[i])) i++, digits++;
  if (i < s.size() && s[i] == '.') {
    i++;
    while (i < s.size() && isDigit(s[i])) i++, digits++;
  }
  if (digits == 0) return 0;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    size_t j = i + 1;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) j++;
    if (j < s.size() && isDigit(s[j])) {
      while (j < s.size() && isDigit(s[j])) j++;
      i = j;
    }
  }
  return i;
}

// Locale-independent: a host app may have called setlocale() with a decimal comma.
std::optional<double> toDouble(std::string_view number) {
  std::istringstream in{std::string(number)};
  in.imbue(std::locale::classic());
  double d = 0;
  in >> d;
  if (in.fail() || !std::isfinite(d)) return std::nullopt;
  return d;
}

std::optional<float> toFloat(double d) {
  float f = static_cast<float>(d);
  if (!std::isfinite(f)) return std::nullopt;
  return f;
}

struct UnitName {
  std::string_view name;
  ValueKind kind;
  uint32_t unit;  // COMPLEX_UNIT_*
  float scale;
};

constexpr uint32_t dim(DimUnit u) { return static_cast<uint32_t>(u); }

// aapt2's unitNames.
constexpr UnitName kUnits[] = {
    {"px", ValueKind::Dimension, dim(DimUnit::Px), 1.0f},
    {"dip", ValueKind::Dimension, dim(DimUnit::Dp), 1.0f},
    {"dp", ValueKind::Dimension, dim(DimUnit::Dp), 1.0f},
    {"sp", ValueKind::Dimension, dim(DimUnit::Sp), 1.0f},
    {"pt", ValueKind::Dimension, dim(DimUnit::Pt), 1.0f},
    {"in", ValueKind::Dimension, dim(DimUnit::In), 1.0f},
    {"mm", ValueKind::Dimension, dim(DimUnit::Mm), 1.0f},
    {"%", ValueKind::Fraction, kComplexUnitFraction, 1.0f / 100},
    {"%p", ValueKind::Fraction, kComplexUnitFractionParent, 1.0f / 100},
};

uint32_t formatOf(ValueKind kind) {
  switch (kind) {
    case ValueKind::Float: return kFormatFloat;
    case ValueKind::Dimension: return kFormatDimension;
    case ValueKind::Fraction: return kFormatFraction;
    default: return 0;
  }
}

}  // namespace

std::optional<Value> parseColor(std::string_view s) {
  s = trim(s);
  if (s.empty() || s[0] != '#') return std::nullopt;
  std::string_view hex = s.substr(1);
  if (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8) {
    return std::nullopt;
  }

  uint32_t nibbles[8] = {};
  for (size_t i = 0; i < hex.size(); i++) {
    int d = hexDigit(hex[i]);
    if (d < 0) return std::nullopt;
    nibbles[i] = static_cast<uint32_t>(d);
  }

  uint32_t argb = 0;
  if (hex.size() <= 4) {
    // #RGB / #ARGB: each digit is doubled, #f80 → #ff8800. Missing alpha is opaque.
    argb = hex.size() == 3 ? 0xffu : nibbles[0] * 0x11u;
    for (size_t i = hex.size() - 3; i < hex.size(); i++) argb = (argb << 8) | (nibbles[i] * 0x11u);
  } else {
    argb = hex.size() == 6 ? 0xffu : 0u;
    for (size_t i = 0; i < hex.size(); i++) argb = (argb << 4) | nibbles[i];
  }
  return Value::color(argb);
}

std::optional<Value> parseBool(std::string_view s) {
  s = trim(s);
  if (s == "true" || s == "True" || s == "TRUE") return Value::boolean(true);
  if (s == "false" || s == "False" || s == "FALSE") return Value::boolean(false);
  return std::nullopt;
}

std::optional<Value> parseInteger(std::string_view s) {
  // Mirrors ResTable::stringToInt.
  s = trim(s);
  size_t i = 0;
  bool negative = false;
  if (i < s.size() && s[i] == '-') {
    negative = true;
    i++;
  }
  if (i >= s.size() || !isDigit(s[i])) return std::nullopt;

  int64_t value = 0;
  bool hex = false;
  if (s[i] == '0' && i + 1 < s.size() && s[i + 1] == 'x') {
    if (negative) return std::nullopt;
    hex = true;
    i += 2;
    if (i == s.size()) return std::nullopt;
    for (; i < s.size(); i++) {
      int d = hexDigit(s[i]);
      if (d < 0) return std::nullopt;
      value = value * 16 + d;
      if (value > UINT32_MAX) return std::nullopt;
    }
  } else {
    for (; i < s.size(); i++) {
      if (!isDigit(s[i])) return std::nullopt;
      value = value * 10 + (s[i] - '0');
      if (negative ? -value < INT32_MIN : value > INT32_MAX) return std::nullopt;
    }
  }
  if (negative) value = -value;
  // Hex keeps the bit pattern: 0xffffffff is -1.
  return Value::integerValue(static_cast<int32_t>(static_cast<uint32_t>(value)), hex);
}

std::optional<Value> parseFloatValue(std::string_view s) {
  // Mirrors ResTable::stringToFloat with parse_unit.
  s = trim(s);
  size_t n = scanNumber(s);
  if (n == 0) return std::nullopt;
  std::optional<double> d = toDouble(s.substr(0, n));
  if (!d) return std::nullopt;

  std::optional<float> f = toFloat(*d);
  if (!f) return std::nullopt;

  std::string_view rest = s.substr(n);
  if (rest.empty()) return Value::floating(*f);
  for (const UnitName& u : kUnits) {
    if (rest != u.name) continue;
    // aapt2 scales in float (f *= 1.0f/100), then packs into the complex format.
    std::optional<uint32_t> data = floatToComplex(*f * u.scale, u.unit);
    if (!data) return std::nullopt;
    return u.kind == ValueKind::Fraction ? Value::fractionFromComplex(*data)
                                         : Value::dimensionFromComplex(*data);
  }
  return std::nullopt;
}

std::optional<Value> parseLiteral(std::string_view s, uint32_t formats) {
  // Same order as aapt2 TryParseItemForAttribute.
  if (formats & kFormatColor) {
    if (auto v = parseColor(s)) return v;
  }
  if (formats & kFormatBoolean) {
    if (auto v = parseBool(s)) return v;
  }
  if (formats & kFormatInteger) {
    if (auto v = parseInteger(s)) return v;
  }
  if (formats & (kFormatFloat | kFormatDimension | kFormatFraction)) {
    if (auto v = parseFloatValue(s); v && (formats & formatOf(v->kind))) return v;
  }
  return std::nullopt;
}

}  // namespace hydroid::res

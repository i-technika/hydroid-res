#include "hydroid/res/ValueParser.h"

#include "hydroid/res/Complex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

// ResTable::stringToFloat copies at most this many characters into its strtof buffer and
// rejects longer text.
constexpr size_t kMaxFloatLength = 126;

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

// A number scanNumber found, as digits × 10^exponent.
struct Decimal {
  bool negative = false;
  std::string digits;  // no leading or trailing zeros; empty for 0
  int exponent = 0;
};

Decimal toDecimal(std::string_view number) {
  Decimal d;
  size_t i = 0;
  if (number[i] == '+' || number[i] == '-') d.negative = number[i++] == '-';
  int64_t exponent = 0;
  bool fraction = false;
  for (; i < number.size() && (isDigit(number[i]) || number[i] == '.'); i++) {
    if (number[i] == '.') {
      fraction = true;
      continue;
    }
    if (fraction) exponent--;
    if (!d.digits.empty() || number[i] != '0') d.digits += number[i];
  }
  if (i < number.size()) {  // [eE][+-]?digits, saturated far past any float
    bool negative = number[++i] == '-';
    if (number[i] == '+' || number[i] == '-') i++;
    int64_t e = 0;
    for (; i < number.size(); i++) e = std::min<int64_t>(e * 10 + (number[i] - '0'), 1000000);
    exponent += negative ? -e : e;
  }
  while (!d.digits.empty() && d.digits.back() == '0') d.digits.pop_back(), exponent++;
  d.exponent = static_cast<int>(std::clamp<int64_t>(exponent, -1000000, 1000000));
  return d;
}

// Just enough of an unsigned big integer to compare a decimal with a float midpoint exactly.
class BigUint {
 public:
  explicit BigUint(uint64_t v) {
    for (; v != 0; v >>= 32) limbs_.push_back(static_cast<uint32_t>(v));
  }
  explicit BigUint(const std::string& digits) {
    for (char c : digits) mulAdd(10, static_cast<uint32_t>(c - '0'));
  }

  void mulPow10(int n) {
    for (; n >= 9; n -= 9) mulAdd(1000000000, 0);
    uint32_t m = 1;
    while (n-- > 0) m *= 10;
    mulAdd(m, 0);
  }
  void mulPow2(int n) {
    for (; n >= 31; n -= 31) mulAdd(1u << 31, 0);
    mulAdd(1u << n, 0);
  }

  friend int compare(const BigUint& a, const BigUint& b) {
    if (a.limbs_.size() != b.limbs_.size()) return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
    for (size_t i = a.limbs_.size(); i-- > 0;) {
      if (a.limbs_[i] != b.limbs_[i]) return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
    }
    return 0;
  }

 private:
  void mulAdd(uint32_t m, uint32_t add) {
    uint64_t carry = add;
    for (uint32_t& limb : limbs_) {
      const uint64_t p = uint64_t{limb} * m + carry;
      limb = static_cast<uint32_t>(p);
      carry = p >> 32;
    }
    if (carry != 0) limbs_.push_back(static_cast<uint32_t>(carry));
  }

  std::vector<uint32_t> limbs_;  // little-endian, no high zero limbs
};

// IEEE 754 binary formats, for rounding a decimal to float as strtof does and to double as
// strtod does.
template <typename F>
struct FloatTraits;
template <>
struct FloatTraits<float> {
  using Bits = uint32_t;
  static constexpr Bits kInf = 0x7f800000;
  static constexpr int kMantissaBits = 23;
  static constexpr int kMinExponent = 1 - 127 - 23;  // of the significand's lowest bit
  static constexpr int kMaxMagnitude = 39;           // 10^39 > FLT_MAX
  static constexpr int kMinMagnitude = -45;          // 10^-46 < half the smallest subnormal
};
template <>
struct FloatTraits<double> {
  using Bits = uint64_t;
  static constexpr Bits kInf = 0x7ff0000000000000;
  static constexpr int kMantissaBits = 52;
  static constexpr int kMinExponent = 1 - 1023 - 52;
  static constexpr int kMaxMagnitude = 309;
  static constexpr int kMinMagnitude = -324;
};

// The non-negative float with these bits as significand × 2^exponent. The bits of infinity give
// the power of two past the largest finite value.
template <typename F>
std::pair<uint64_t, int> floatParts(typename FloatTraits<F>::Bits bits) {
  using T = FloatTraits<F>;
  const auto e = static_cast<int>(bits >> T::kMantissaBits);
  const uint64_t m = bits & ((uint64_t{1} << T::kMantissaBits) - 1);
  if (e == 0) return {m, T::kMinExponent};
  return {m | (uint64_t{1} << T::kMantissaBits), e - 1 + T::kMinExponent};
}

// Sign of |d| − (midpoint of the floats with bits n and n + 1), where `scaled` is d's digits
// times 10^exponent if the exponent is not negative, made once for every midpoint.
template <typename F>
int compareWithMidpoint(const BigUint& scaled, int exponent, typename FloatTraits<F>::Bits n) {
  const auto [s0, e0] = floatParts<F>(n);
  const auto [s1, e1] = floatParts<F>(n + 1);
  BigUint rhs(s0 + (s1 << (e1 - e0)));  // the midpoint is rhs × 2^(e0 - 1)
  const int e2 = e0 - 1;
  if (exponent < 0) rhs.mulPow10(-exponent);
  if (e2 >= 0) {
    rhs.mulPow2(e2);
    return compare(scaled, rhs);
  }
  BigUint lhs = scaled;
  lhs.mulPow2(-e2);
  return compare(lhs, rhs);
}

// The decimal rounded to the nearest F, ties to even, as strtof and strtod do. Going through a
// double for a float would round twice: 1.0000000596046447753906251 is a hair above the midpoint
// of 1 and the next float, but the nearest double is that midpoint, which then ties down to 1.
// Never touches the C locale, which a host app may have set to a decimal comma. nullopt if the
// number rounds to infinity. Not std::from_chars: libc++ has it for floats only since LLVM 20,
// and the standard libraries disagree on what it leaves for overflow and underflow.
template <typename F>
std::optional<F> roundDecimal(const Decimal& d) {
  using T = FloatTraits<F>;
  using Bits = typename T::Bits;
  const F sign = d.negative ? F{-1} : F{1};
  if (d.digits.empty()) return sign * F{0};
  // d is in [10^(magnitude - 1), 10^magnitude).
  const int64_t magnitude = int64_t{d.exponent} + static_cast<int64_t>(d.digits.size());
  if (magnitude > T::kMaxMagnitude) return std::nullopt;
  if (magnitude < T::kMinMagnitude) return sign * F{0};

  // Fast path: both operands are exact, so one operation rounds once.
  constexpr F kPow10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10};
  if (d.digits.size() <= 7 && d.exponent >= -10 && d.exponent <= 10) {
    const auto v = static_cast<F>(std::stoul(d.digits));
    return sign * (d.exponent >= 0 ? v * kPow10[d.exponent] : v / kPow10[-d.exponent]);
  }

  // Otherwise start from an estimate within a few ulps and step to the right value, comparing
  // exactly with the midpoints on either side. The estimate is the leading digits as a fraction
  // in [0.1, 1), scaled in two steps so that neither power of ten underflows on the way to a
  // subnormal.
  const size_t lead = std::min<size_t>(d.digits.size(), 19);
  const double fraction = static_cast<double>(std::stoull(d.digits.substr(0, lead))) /
                          std::pow(10.0, static_cast<double>(lead));
  const auto half = static_cast<double>(magnitude / 2);
  const double estimate =
      fraction * std::pow(10.0, half) * std::pow(10.0, static_cast<double>(magnitude) - half);
  constexpr Bits inf = T::kInf;
  Bits n;
  const F start = static_cast<F>(std::min(estimate, double{std::numeric_limits<F>::max()}));
  std::memcpy(&n, &start, sizeof n);
  BigUint scaled(d.digits);
  if (d.exponent >= 0) scaled.mulPow10(d.exponent);
  for (;;) {
    int c = n < inf ? compareWithMidpoint<F>(scaled, d.exponent, n) : -1;
    if (c > 0 || (c == 0 && (n & 1))) {
      n++;
      continue;
    }
    c = n > 0 ? compareWithMidpoint<F>(scaled, d.exponent, n - 1) : 1;
    if (c < 0 || (c == 0 && (n & 1))) {
      n--;
      continue;
    }
    break;
  }
  if (n >= inf) return std::nullopt;
  F f;
  std::memcpy(&f, &n, sizeof f);
  return sign * f;
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

// The format a value from parseFloatValue needs. Every kind is listed, so a new ValueKind that
// parseFloatValue can make doesn't silently get 0 (-Wswitch, C4062).
uint32_t formatOf(ValueKind kind) {
  switch (kind) {
    case ValueKind::Float: return kFormatFloat;
    case ValueKind::Dimension: return kFormatDimension;
    case ValueKind::Fraction: return kFormatFraction;
    case ValueKind::Null:
    case ValueKind::Reference:
    case ValueKind::AttrRef:
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::Color:
    case ValueKind::Int:
    case ValueKind::Bool:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      return 0;
  }
  return 0;
}

constexpr uint32_t kFloatFormats = kFormatFloat | kFormatDimension | kFormatFraction;

// Whether the float `f` that `number` parsed to is within 1 of the same text read as a double,
// which aapt2 asks of a plain float when the formats also allow something else.
bool closeToDouble(std::string_view number, float f) {
  // Below 2^24 a float is within half of 1 of the decimal, so only large floats are read again.
  if (std::fabs(f) < 16777216.0f) return true;
  std::optional<double> d = roundDecimal<double>(toDecimal(trim(number)));
  return d && std::fabs(static_cast<double>(f) - *d) < 1;
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

  if (hex.size() <= 4) {
    // #RGB / #ARGB: each digit is doubled, #f80 → #ff8800. Missing alpha is opaque.
    uint32_t argb = hex.size() == 3 ? 0xffu : nibbles[0] * 0x11u;
    for (size_t i = hex.size() - 3; i < hex.size(); i++) argb = (argb << 8) | (nibbles[i] * 0x11u);
    return Value::color(argb, hex.size() == 3 ? ColorFormat::Rgb4 : ColorFormat::Argb4);
  }
  uint32_t argb = hex.size() == 6 ? 0xffu : 0u;
  for (size_t i = 0; i < hex.size(); i++) argb = (argb << 4) | nibbles[i];
  return Value::color(argb, hex.size() == 6 ? ColorFormat::Rgb8 : ColorFormat::Argb8);
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
  return Value::integer(static_cast<int32_t>(static_cast<uint32_t>(value)),
                        hex ? IntFormat::Hex : IntFormat::Dec);
}

std::optional<Value> parseFloatValue(std::string_view s) {
  // Mirrors ResTable::stringToFloat with parse_unit.
  s = trim(s);
  if (s.size() > kMaxFloatLength) return std::nullopt;
  size_t n = scanNumber(s);
  if (n == 0) return std::nullopt;
  std::optional<float> f = roundDecimal<float>(toDecimal(s.substr(0, n)));
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
  if (formats & kFloatFormats) {
    auto v = parseFloatValue(s);
    if (!v || !(formats & formatOf(v->kind()))) return std::nullopt;
    // aapt2 keeps the text as a string instead if the float is 1 or more off: 9638178525 under
    // kFormatAny, whose nearest float is 9638178816. Any format past float/dimension/fraction
    // asks for the check, even one that cannot hold the string, such as float|reference.
    if (v->kind() == ValueKind::Float && (formats & ~kFloatFormats) &&
        !closeToDouble(s, v->floatValue())) {
      return std::nullopt;
    }
    return v;
  }
  return std::nullopt;
}

}  // namespace hydroid::res

#pragma once

#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <type_traits>

#include "hydroid/res/ResId.h"

namespace hydroid::res {

enum class ValueKind : uint8_t {
  Null, Reference, AttrRef,          // @type/name, ?attr/name
  String, StyledString,              // <b> 등 서식 있는 문자열은 span 목록 포함
  Color, Dimension, Fraction, Float, Int, Bool,
  File,                              // layout/foo.xml, drawable/bg.png 같은 파일
  Style, Array, Plurals, AttrDecl, StyleableDecl
};

// Same values as Android's TypedValue.COMPLEX_UNIT_PX ... COMPLEX_UNIT_MM.
enum class DimUnit : uint8_t { Px = 0, Dp = 1, Sp = 2, Pt = 3, In = 4, Mm = 5 };

// How a color was written; same values as Android's TypedValue.TYPE_INT_COLOR_*.
enum class ColorFormat : uint8_t {
  Argb8 = 28,  // #AARRGGBB
  Rgb8 = 29,   // #RRGGBB
  Argb4 = 30,  // #ARGB
  Rgb4 = 31,   // #RGB
};

// How an integer was written; same values as Android's TypedValue.TYPE_INT_DEC / TYPE_INT_HEX.
enum class IntFormat : uint8_t {
  Dec = 16,  // 42, -42
  Hex = 17,  // 0x2a
};

// A value of one ValueKind. Only the factories set the kind, so the data always belongs to it.
//
// Factories are named for the kind they make: color, integer, boolean, floating (Int, Bool and
// Float are C++ keywords), reference, string, ... Each takes the value itself, except the
// *FromComplex ones, which take Res_value data. Accessors are named for what they return and
// assert the kind: argb() of a Dimension stops a debug build.
class Value {
 public:
  Value() : kind_(ValueKind::Null), index_(0) {}

  static Value reference(ResId id) { return withRef(ValueKind::Reference, id); }
  static Value attrRef(ResId id) { return withRef(ValueKind::AttrRef, id); }
  // `argb` is 0xAARRGGBB whatever the format: #f80 is color(0xffff8800, ColorFormat::Rgb4).
  static Value color(uint32_t argb, ColorFormat format) {
    Value v(ValueKind::Color);
    v.color_ = {argb, format};
    return v;
  }
  static Value integer(int32_t value, IntFormat format = IntFormat::Dec) {
    Value v(ValueKind::Int);
    v.integer_ = {value, format};
    return v;
  }
  static Value boolean(bool value) {
    Value v(ValueKind::Bool);
    v.bool_ = value;
    return v;
  }
  static Value floating(float value) {
    Value v(ValueKind::Float);
    v.float_ = value;
    return v;
  }
  // `data` is Res_value data with its unit bits, as aapt2 writes it (16dp → 0x1001, not 16): get
  // it from parseFloatValue or floatToComplex. Units are not checked; like Android, an unknown
  // one is kept and converts to 0.
  static Value dimensionFromComplex(uint32_t data) {
    return withComplex(ValueKind::Dimension, data);
  }
  static Value fractionFromComplex(uint32_t data) {
    return withComplex(ValueKind::Fraction, data);
  }
  // A float is a number, not complex data: dimensionFromComplex(1.5f) would store 0x1 (0px).
  template <typename F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0>
  static Value dimensionFromComplex(F) = delete;
  template <typename F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0>
  static Value fractionFromComplex(F) = delete;
  // Kinds held as a position in the string pool or the compound value pool.
  static Value string(uint32_t index) { return withIndex(ValueKind::String, index); }
  static Value styledString(uint32_t index) { return withIndex(ValueKind::StyledString, index); }
  static Value file(uint32_t index) { return withIndex(ValueKind::File, index); }
  static Value style(uint32_t index) { return withIndex(ValueKind::Style, index); }
  static Value array(uint32_t index) { return withIndex(ValueKind::Array, index); }
  static Value plurals(uint32_t index) { return withIndex(ValueKind::Plurals, index); }
  static Value attrDecl(uint32_t index) { return withIndex(ValueKind::AttrDecl, index); }
  static Value styleableDecl(uint32_t index) { return withIndex(ValueKind::StyleableDecl, index); }

  ValueKind kind() const { return kind_; }

  // Reference, AttrRef
  ResId ref() const {
    assert((kind_ == ValueKind::Reference || kind_ == ValueKind::AttrRef) && "not a reference");
    return ref_;
  }
  // Color
  uint32_t argb() const {
    assert(kind_ == ValueKind::Color && "not a Color");
    return color_.argb;
  }
  ColorFormat colorFormat() const {
    assert(kind_ == ValueKind::Color && "not a Color");
    return color_.format;
  }
  // Int
  int32_t intValue() const {
    assert(kind_ == ValueKind::Int && "not an Int");
    return integer_.value;
  }
  IntFormat intFormat() const {
    assert(kind_ == ValueKind::Int && "not an Int");
    return integer_.format;
  }
  // Bool
  bool boolValue() const {
    assert(kind_ == ValueKind::Bool && "not a Bool");
    return bool_;
  }
  // Float
  float floatValue() const {
    assert(kind_ == ValueKind::Float && "not a Float");
    return float_;
  }
  // Dimension, Fraction: Res_value data, see Complex.h
  uint32_t complexData() const {
    assert((kind_ == ValueKind::Dimension || kind_ == ValueKind::Fraction) &&
           "not a Dimension or Fraction");
    return complex_;
  }
  // String, StyledString, File, Style, Array, Plurals, AttrDecl, StyleableDecl
  uint32_t index() const {
    assert(holdsIndex(kind_) && "not a pooled kind");
    return index_;
  }

 private:
  // Named so they can sit in the anonymous union (unnamed structs there are an extension).
  struct ColorData { uint32_t argb; ColorFormat format; };  // argb is always 0xAARRGGBB
  struct IntData { int32_t value; IntFormat format; };

  explicit Value(ValueKind kind) : kind_(kind), index_(0) {}

  static Value withRef(ValueKind kind, ResId id) {
    Value v(kind);
    v.ref_ = id;
    return v;
  }
  static Value withComplex(ValueKind kind, uint32_t data) {
    Value v(kind);
    v.complex_ = data;
    return v;
  }
  static Value withIndex(ValueKind kind, uint32_t index) {
    Value v(kind);
    v.index_ = index;
    return v;
  }
  static bool holdsIndex(ValueKind kind);

  ValueKind kind_;
  union {
    ResId     ref_;      // Reference, AttrRef
    ColorData color_;    // Color
    IntData   integer_;  // Int
    bool      bool_;     // Bool
    uint32_t  complex_;  // Dimension, Fraction
    float     float_;    // Float
    uint32_t  index_;    // 문자열 풀 또는 복합값 풀 위치
  };
};

bool operator==(const Value& a, const Value& b);
inline bool operator!=(const Value& a, const Value& b) { return !(a == b); }

// For logs and test failures: Dimension(16dp, 0x00001001), Color(0xff6200ee, Rgb8). Floats get
// every digit, so values one ulp apart print differently. Declared here rather than in the tests
// so that every file that sees Value sees it too, and gtest prints Values the same way in all.
std::ostream& operator<<(std::ostream& os, const Value& v);

const char* toString(DimUnit unit);  // "px", "dp", ...

}  // namespace hydroid::res

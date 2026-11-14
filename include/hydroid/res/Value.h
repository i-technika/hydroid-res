#pragma once

#include <cstdint>
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

// Named so they can sit in Value's anonymous union (unnamed structs there are an extension).
struct IntData { int32_t v; bool hex; };         // hex = written as 0x.. (TYPE_INT_HEX)
struct ColorData { uint32_t argb; ColorFormat format; };  // argb is always 0xAARRGGBB

struct Value {
  ValueKind kind = ValueKind::Null;
  union {
    ResId        ref;                           // Reference, AttrRef
    ColorData    color;                         // Color
    IntData      integer;                       // Int
    bool         b;                             // Bool
    uint32_t     complex;                       // Dimension, Fraction: Res_value data, see Complex.h
    float        f;                             // Float
    uint32_t index;                             // 문자열 풀 또는 복합값 풀 위치
  };

  Value() : index(0) {}

  // `argb` is 0xAARRGGBB whatever the format: #f80 is colorValue(0xffff8800, ColorFormat::Rgb4).
  static Value colorValue(uint32_t argb, ColorFormat format) {
    Value v;
    v.kind = ValueKind::Color;
    v.color = {argb, format};
    return v;
  }
  static Value integerValue(int32_t value, bool isHex = false) {
    Value v;
    v.kind = ValueKind::Int;
    v.integer = {value, isHex};
    return v;
  }
  static Value boolean(bool value) {
    Value v;
    v.kind = ValueKind::Bool;
    v.b = value;
    return v;
  }
  // `data` is Res_value data with its unit bits, as aapt2 writes it (16dp → 0x1001, not 16): get
  // it from parseFloatValue or floatToComplex. Units are not checked; like Android, an unknown
  // one is kept and converts to 0.
  static Value dimensionFromComplex(uint32_t data) {
    Value v;
    v.kind = ValueKind::Dimension;
    v.complex = data;
    return v;
  }
  static Value fractionFromComplex(uint32_t data) {
    Value v;
    v.kind = ValueKind::Fraction;
    v.complex = data;
    return v;
  }
  // A float is a number, not complex data: dimensionFromComplex(1.5f) would store 0x1 (0px).
  template <typename F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0>
  static Value dimensionFromComplex(F) = delete;
  template <typename F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0>
  static Value fractionFromComplex(F) = delete;
  static Value floating(float value) {
    Value v;
    v.kind = ValueKind::Float;
    v.f = value;
    return v;
  }
};

bool operator==(const Value& a, const Value& b);
inline bool operator!=(const Value& a, const Value& b) { return !(a == b); }

const char* toString(DimUnit unit);  // "px", "dp", ...

}  // namespace hydroid::res

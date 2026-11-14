#pragma once

#include <cstdint>

#include "hydroid/res/ResId.h"

namespace hydroid::res {

enum class ValueKind : uint8_t {
  Null, Reference, AttrRef,          // @type/name, ?attr/name
  String, StyledString,              // <b> 등 서식 있는 문자열은 span 목록 포함
  Color, Dimension, Fraction, Float, Int, Bool,
  File,                              // layout/foo.xml, drawable/bg.png 같은 파일
  Style, Array, Plurals, AttrDecl, StyleableDecl
};

enum class DimUnit : uint8_t { Px, Dp, Sp, Pt, In, Mm };

struct Value {
  ValueKind kind = ValueKind::Null;
  union {
    ResId    ref;                               // Reference, AttrRef
    uint32_t argb;                              // Color
    struct { int32_t v; bool hex; } integer;    // Int; hex = written as 0x.. (TYPE_INT_HEX)
    bool     b;                                 // Bool
    struct { float v; DimUnit unit; } dim;      // Dimension
    struct { float v; bool parent; } fraction;  // Fraction: "50%" → 0.5, "%p" sets parent
    float    f;                                 // Float
    uint32_t index;                             // 문자열 풀 또는 복합값 풀 위치
  };

  Value() : index(0) {}

  static Value color(uint32_t value) {
    Value v;
    v.kind = ValueKind::Color;
    v.argb = value;
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
  static Value dimension(float value, DimUnit u) {
    Value v;
    v.kind = ValueKind::Dimension;
    v.dim = {value, u};
    return v;
  }
  static Value fractionValue(float value, bool ofParent) {
    Value v;
    v.kind = ValueKind::Fraction;
    v.fraction = {value, ofParent};
    return v;
  }
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

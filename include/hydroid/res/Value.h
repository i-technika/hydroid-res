#pragma once

#include <cstdint>
#include <iosfwd>
#include <type_traits>

#include "hydroid/res/Export.h"
#include "hydroid/res/ResId.h"

namespace hydroid::res {

enum class ValueKind : uint8_t {
  Null, Empty,                       // @null, @empty: see Value::null() and Value::empty()
  Reference, AttrRef,                // @type/name, ?attr/name
  String, StyledString,              // StyledString: text with spans such as <b>
  Color, Dimension, Fraction, Float, Int, Bool,
  File,                              // a file such as layout/foo.xml or drawable/bg.png
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
// Factories are named for the kind they make: null, empty, color, integer, boolean, floating (Int,
// Bool and Float are C++ keywords), reference, string, ... Each takes the value itself, except the
// *FromComplex ones, which take Res_value data. Accessors are named for what they return and
// check the kind: argb() of a Dimension stops a debug build of the library. A release build of
// the library doesn't check, and reads the union as it is.
class Value {
 public:
  // A default Value is @null.
  Value() : Value(ValueKind::Null) {}
  // @null: no value. aapt2 writes it as a reference to 0 (TYPE_REFERENCE, data 0), which the
  // runtime reads back as undefined (TYPE_NULL, data 0): TypedArray.hasValueOrEmpty is false.
  static Value null() { return Value(ValueKind::Null); }
  // @empty: a value that is there but empty (TYPE_NULL, data DATA_NULL_EMPTY = 1), so
  // TypedArray.hasValueOrEmpty is true, unlike for @null.
  static Value empty() { return Value(ValueKind::Empty); }

  // `id` is not 0: a reference to 0 is how aapt2 writes @null, so it reads back as null(), and
  // reference(0) or attrRef(0) would not. They stop a debug build of the library.
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
  // These convert silently: integer(true) would be Int(1), integer(1.5f) Int(1), boolean(42)
  // Bool(true).
  template <typename T,
            std::enable_if_t<std::is_same_v<T, bool> || std::is_floating_point_v<T>, int> = 0>
  static Value integer(T, IntFormat = IntFormat::Dec) = delete;
  template <typename T, std::enable_if_t<!std::is_same_v<T, bool>, int> = 0>
  static Value boolean(T) = delete;
  static Value floating(float value) {
    Value v(ValueKind::Float);
    v.float_ = value;
    return v;
  }
  // `data` is Res_value data with its unit bits, as aapt2 writes it (16dp is 0x1001, not 16): get
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
    expect(Payload::Ref, "not a reference");
    return ref_;
  }
  // Color
  uint32_t argb() const {
    expect(Payload::Color, "not a Color");
    return color_.argb;
  }
  ColorFormat colorFormat() const {
    expect(Payload::Color, "not a Color");
    return color_.format;
  }
  // Int
  int32_t intValue() const {
    expect(Payload::Int, "not an Int");
    return integer_.value;
  }
  IntFormat intFormat() const {
    expect(Payload::Int, "not an Int");
    return integer_.format;
  }
  // Bool
  bool boolValue() const {
    expect(Payload::Bool, "not a Bool");
    return bool_;
  }
  // Float
  float floatValue() const {
    expect(Payload::Float, "not a Float");
    return float_;
  }
  // Dimension, Fraction: Res_value data, see Complex.h
  uint32_t complexData() const {
    expect(Payload::Complex, "not a Dimension or Fraction");
    return complex_;
  }
  // String, StyledString, File, Style, Array, Plurals, AttrDecl, StyleableDecl
  uint32_t index() const {
    expect(Payload::Index, "not a pooled kind");
    return index_;
  }

 private:
  // MSVC wants the export on the friend declarations too, as they come first.
  friend HYDROID_RES_API bool operator==(const Value& a, const Value& b);
  friend HYDROID_RES_API std::ostream& operator<<(std::ostream& os, const Value& v);

  // The union member a kind holds. This is the one place that sorts kinds into members; the
  // accessors, operator== and operator<< all go through it.
  enum class Payload : uint8_t { None, Ref, Color, Int, Bool, Float, Complex, Index };
  static constexpr Payload payloadOf(ValueKind kind) {
    switch (kind) {
      case ValueKind::Null:
      case ValueKind::Empty: return Payload::None;
      case ValueKind::Reference:
      case ValueKind::AttrRef: return Payload::Ref;
      case ValueKind::Color: return Payload::Color;
      case ValueKind::Int: return Payload::Int;
      case ValueKind::Bool: return Payload::Bool;
      case ValueKind::Float: return Payload::Float;
      case ValueKind::Dimension:
      case ValueKind::Fraction: return Payload::Complex;
      case ValueKind::String:
      case ValueKind::StyledString:
      case ValueKind::File:
      case ValueKind::Style:
      case ValueKind::Array:
      case ValueKind::Plurals:
      case ValueKind::AttrDecl:
      case ValueKind::StyleableDecl: return Payload::Index;
    }
    // The factories never set a kind out of range, but a raw byte copy (memcpy, bit_cast) can,
    // since Value is trivially copyable. Such a Value holds nothing, so it equals one with the
    // same kind byte, and every accessor but kind() fails on it.
    return Payload::None;
  }

  // The check is inline but its failure is not: an assert() here would compile differently in
  // each file that includes this header, by that file's NDEBUG, and break the one definition rule
  // for every accessor. kindMismatch stops only when the library itself is a debug build.
  void expect(Payload payload, const char* what) const {
    if (payloadOf(kind_) != payload) kindMismatch(what);
  }
  // Exported although private: the inline accessors above call it from the consumer's code.
  static HYDROID_RES_API void kindMismatch(const char* what);

  // Named so they can sit in the anonymous union (unnamed structs there are an extension).
  struct ColorData { uint32_t argb; ColorFormat format; };  // argb is always 0xAARRGGBB
  struct IntData { int32_t value; IntFormat format; };

  explicit Value(ValueKind kind) : kind_(kind), index_(0) {}

  static Value withRef(ValueKind kind, ResId id) {
    if (id == 0) kindMismatch("reference to 0 (@null is Value::null())");
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

  ValueKind kind_;
  union {
    ResId     ref_;      // Reference, AttrRef
    ColorData color_;    // Color
    IntData   integer_;  // Int
    bool      bool_;     // Bool
    uint32_t  complex_;  // Dimension, Fraction
    float     float_;    // Float
    uint32_t  index_;    // pooled kinds: position in the string or compound value pool
  };
};

HYDROID_RES_API bool operator==(const Value& a, const Value& b);
inline bool operator!=(const Value& a, const Value& b) { return !(a == b); }

// For logs and test failures: Dimension(16dp, 0x00001001), Color(0xff6200ee, Rgb8). Floats get
// every digit, so values one ulp apart print differently. Declared here rather than in the tests
// so that every file that sees Value sees it too, and gtest prints Values the same way in all.
HYDROID_RES_API std::ostream& operator<<(std::ostream& os, const Value& v);

HYDROID_RES_API const char* toString(DimUnit unit);  // "px", "dp", ...

}  // namespace hydroid::res

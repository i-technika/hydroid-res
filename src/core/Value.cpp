#include "hydroid/res/Value.h"

#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>

#include "hydroid/res/Complex.h"

namespace hydroid::res {

namespace {

const char* kindName(ValueKind kind) {
  switch (kind) {
    case ValueKind::Null: return "Null";
    case ValueKind::Reference: return "Reference";
    case ValueKind::AttrRef: return "AttrRef";
    case ValueKind::String: return "String";
    case ValueKind::StyledString: return "StyledString";
    case ValueKind::Color: return "Color";
    case ValueKind::Dimension: return "Dimension";
    case ValueKind::Fraction: return "Fraction";
    case ValueKind::Float: return "Float";
    case ValueKind::Int: return "Int";
    case ValueKind::Bool: return "Bool";
    case ValueKind::File: return "File";
    case ValueKind::Style: return "Style";
    case ValueKind::Array: return "Array";
    case ValueKind::Plurals: return "Plurals";
    case ValueKind::AttrDecl: return "AttrDecl";
    case ValueKind::StyleableDecl: return "StyleableDecl";
  }
  return "?";
}

const char* formatName(ColorFormat format) {
  switch (format) {
    case ColorFormat::Argb8: return "Argb8";
    case ColorFormat::Rgb8: return "Rgb8";
    case ColorFormat::Argb4: return "Argb4";
    case ColorFormat::Rgb4: return "Rgb4";
  }
  return "?";
}

uint32_t bitsOf(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof bits);
  return bits;
}

}  // namespace

bool operator==(const Value& a, const Value& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case ValueKind::Null:
      return true;
    case ValueKind::Reference:
    case ValueKind::AttrRef:
      return a.ref == b.ref;
    case ValueKind::Color:
      return a.color.argb == b.color.argb && a.color.format == b.color.format;
    case ValueKind::Int:
      return a.integer.v == b.integer.v && a.integer.hex == b.integer.hex;
    case ValueKind::Bool:
      return a.b == b.b;
    case ValueKind::Dimension:
    case ValueKind::Fraction:
      return a.complex == b.complex;
    case ValueKind::Float:
      // By bits, as Res_value data holds it: -0 is not 0 (0x80000000), and a NaN equals itself.
      return bitsOf(a.f) == bitsOf(b.f);
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      return a.index == b.index;
  }
  return false;
}

std::ostream& operator<<(std::ostream& os, const Value& v) {
  // Formatted on a stream of its own, so the caller's flags, precision and locale neither change
  // the output nor are changed by it.
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<float>::max_digits10);
  const auto hex = [&out](uint32_t x) {
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << x << std::dec;
  };

  out << kindName(v.kind) << '(';
  switch (v.kind) {
    case ValueKind::Null:
      break;
    case ValueKind::Reference:
    case ValueKind::AttrRef:
      hex(v.ref);
      break;
    case ValueKind::Color:
      hex(v.color.argb);
      out << ", " << formatName(v.color.format);
      break;
    case ValueKind::Int:
      out << v.integer.v << (v.integer.hex ? ", hex" : "");
      break;
    case ValueKind::Bool:
      out << (v.b ? "true" : "false");
      break;
    case ValueKind::Dimension:
      out << complexToFloat(v.complex) << toString(static_cast<DimUnit>(complexUnit(v.complex)))
          << ", ";
      hex(v.complex);
      break;
    case ValueKind::Fraction:
      out << complexToFloat(v.complex)
          << (complexUnit(v.complex) == kComplexUnitFractionParent ? ", parent" : "") << ", ";
      hex(v.complex);
      break;
    case ValueKind::Float:
      out << v.f;
      break;
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      out << "index " << v.index;
      break;
  }
  out << ')';
  return os << out.str();
}

const char* toString(DimUnit unit) {
  switch (unit) {
    case DimUnit::Px: return "px";
    case DimUnit::Dp: return "dp";
    case DimUnit::Sp: return "sp";
    case DimUnit::Pt: return "pt";
    case DimUnit::In: return "in";
    case DimUnit::Mm: return "mm";
  }
  return "?";
}

}  // namespace hydroid::res

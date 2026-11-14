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

bool Value::holdsIndex(ValueKind kind) {
  switch (kind) {
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      return true;
    case ValueKind::Null:
    case ValueKind::Reference:
    case ValueKind::AttrRef:
    case ValueKind::Color:
    case ValueKind::Dimension:
    case ValueKind::Fraction:
    case ValueKind::Float:
    case ValueKind::Int:
    case ValueKind::Bool:
      return false;
  }
  return false;
}

bool operator==(const Value& a, const Value& b) {
  if (a.kind() != b.kind()) return false;
  switch (a.kind()) {
    case ValueKind::Null:
      return true;
    case ValueKind::Reference:
    case ValueKind::AttrRef:
      return a.ref() == b.ref();
    case ValueKind::Color:
      return a.argb() == b.argb() && a.colorFormat() == b.colorFormat();
    case ValueKind::Int:
      return a.intValue() == b.intValue() && a.intFormat() == b.intFormat();
    case ValueKind::Bool:
      return a.boolValue() == b.boolValue();
    case ValueKind::Dimension:
    case ValueKind::Fraction:
      return a.complexData() == b.complexData();
    case ValueKind::Float:
      // By bits, as Res_value data holds it: -0 is not 0 (0x80000000), and a NaN equals itself.
      return bitsOf(a.floatValue()) == bitsOf(b.floatValue());
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      return a.index() == b.index();
  }
  return false;  // unreachable: only the factories set the kind
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

  out << kindName(v.kind()) << '(';
  switch (v.kind()) {
    case ValueKind::Null:
      break;
    case ValueKind::Reference:
    case ValueKind::AttrRef:
      hex(v.ref());
      break;
    case ValueKind::Color:
      hex(v.argb());
      out << ", " << formatName(v.colorFormat());
      break;
    case ValueKind::Int:
      out << v.intValue() << (v.intFormat() == IntFormat::Hex ? ", hex" : "");
      break;
    case ValueKind::Bool:
      out << (v.boolValue() ? "true" : "false");
      break;
    case ValueKind::Dimension: {
      const uint32_t data = v.complexData();
      out << complexToFloat(data) << toString(static_cast<DimUnit>(complexUnit(data))) << ", ";
      hex(data);
      break;
    }
    case ValueKind::Fraction: {
      const uint32_t data = v.complexData();
      out << complexToFloat(data)
          << (complexUnit(data) == kComplexUnitFractionParent ? ", parent" : "") << ", ";
      hex(data);
      break;
    }
    case ValueKind::Float:
      out << v.floatValue();
      break;
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      out << "index " << v.index();
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

#include "hydroid/res/Value.h"

namespace hydroid::res {

bool operator==(const Value& a, const Value& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case ValueKind::Null:
      return true;
    case ValueKind::Reference:
    case ValueKind::AttrRef:
      return a.ref == b.ref;
    case ValueKind::Color:
      return a.argb == b.argb;
    case ValueKind::Int:
      return a.integer.v == b.integer.v && a.integer.hex == b.integer.hex;
    case ValueKind::Bool:
      return a.b == b.b;
    case ValueKind::Dimension:
      return a.dim.v == b.dim.v && a.dim.unit == b.dim.unit;
    case ValueKind::Fraction:
      return a.fraction.v == b.fraction.v && a.fraction.parent == b.fraction.parent;
    case ValueKind::Float:
      return a.f == b.f;
    default:
      return a.index == b.index;
  }
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

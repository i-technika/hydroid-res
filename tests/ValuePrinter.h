// How gtest prints a Value in failure messages. Every test file that compares Values must include
// this: gtest's printer for Value is a template, and a file without PrintTo in sight instantiates
// the byte-dump fallback, which the linker may then use for every file.

#pragma once

#include <ostream>

#include "hydroid/res/Complex.h"
#include "hydroid/res/Value.h"

namespace hydroid::res {

inline void PrintTo(const Value& v, std::ostream* os) {
  switch (v.kind) {
    case ValueKind::Color:
      *os << "Color(0x" << std::hex << v.color.argb << std::dec << ", type "
          << static_cast<int>(v.color.format) << ")";
      break;
    case ValueKind::Int: *os << "Int(" << v.integer.v << (v.integer.hex ? ", hex)" : ")"); break;
    case ValueKind::Bool: *os << "Bool(" << (v.b ? "true" : "false") << ")"; break;
    case ValueKind::Dimension:
      *os << "Dimension(" << complexToFloat(v.complex)
          << toString(static_cast<DimUnit>(complexUnit(v.complex))) << ", 0x" << std::hex
          << v.complex << std::dec << ")";
      break;
    case ValueKind::Fraction:
      *os << "Fraction(" << complexToFloat(v.complex)
          << (complexUnit(v.complex) == kComplexUnitFractionParent ? ", parent" : "") << ", 0x"
          << std::hex << v.complex << std::dec << ")";
      break;
    case ValueKind::Float: *os << "Float(" << v.f << ")"; break;
    default: *os << "Value(kind " << static_cast<int>(v.kind) << ")"; break;
  }
}

}  // namespace hydroid::res

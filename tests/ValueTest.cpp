// Value's operator== for the kinds the parser tests don't build, and its operator<<, which is also
// what gtest shows for a Value in failure messages.

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "hydroid/res/Complex.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

std::string print(const Value& v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

Value withIndex(ValueKind kind, uint32_t index) {
  Value v;
  v.kind = kind;
  v.index = index;
  return v;
}

float floatFromBits(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

// A Value of `kind` whose Res_value data is `data` (0 or 1), or nullopt past the last kind. No
// default, so a new ValueKind doesn't build (-Wswitch, C4062) until it is added here, and then
// everyKind() has it.
std::optional<Value> sample(ValueKind kind, uint32_t data) {
  switch (kind) {
    case ValueKind::Null: return Value();
    case ValueKind::Reference:
    case ValueKind::AttrRef: {
      Value v;
      v.kind = kind;
      v.ref = data;
      return v;
    }
    case ValueKind::Color: return Value::colorValue(data, ColorFormat::Argb8);
    case ValueKind::Dimension: return Value::dimensionFromComplex(data);
    case ValueKind::Fraction: return Value::fractionFromComplex(data);
    case ValueKind::Float: return Value::floating(floatFromBits(data));
    case ValueKind::Int: return Value::integerValue(static_cast<int32_t>(data));
    case ValueKind::Bool: return Value::boolean(data != 0);
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl:
      return withIndex(kind, data);
  }
  return std::nullopt;
}

std::vector<ValueKind> everyKind() {
  std::vector<ValueKind> kinds;
  for (int k = 0; sample(static_cast<ValueKind>(k), 0); ++k) {
    kinds.push_back(static_cast<ValueKind>(k));
  }
  return kinds;
}

TEST(ValueEquality, EveryKindComparesItsData) {
  for (ValueKind kind : everyKind()) {
    SCOPED_TRACE(testing::PrintToString(*sample(kind, 1)));
    EXPECT_EQ(sample(kind, 1), sample(kind, 1));
    if (kind != ValueKind::Null) {
      EXPECT_NE(sample(kind, 1), sample(kind, 0));
    }
  }
}

// Every kind but Null holds data 1 in the same union bits, so only the kind tells them apart.
TEST(ValueEquality, SameDataDifferentKindDiffers) {
  const std::vector<ValueKind> kinds = everyKind();
  for (ValueKind a : kinds) {
    for (ValueKind b : kinds) {
      if (a != b) {
        EXPECT_NE(sample(a, 1), sample(b, 1));
      }
    }
  }
}

TEST(ValueEquality, ReferenceAndNull) {
  Value ref;
  ref.kind = ValueKind::Reference;
  ref.ref = 0x7f010001;
  Value attr = ref;
  attr.kind = ValueKind::AttrRef;
  Value other = ref;
  other.ref = 0x7f010002;
  EXPECT_EQ(ref, ref);
  EXPECT_NE(ref, other);
  EXPECT_NE(ref, attr);
  EXPECT_EQ(Value(), Value());
}

TEST(ValueEquality, FloatComparesBits) {
  EXPECT_EQ(Value::floating(1.5f), Value::floating(1.5f));
  EXPECT_NE(Value::floating(-0.0f), Value::floating(0.0f));
  const Value nan = Value::floating(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(nan, nan);
}

TEST(PrintValue, IsWhatGtestShows) {
  EXPECT_EQ(testing::PrintToString(Value::boolean(true)), "Bool(true)");
}

TEST(PrintValue, Bool) {
  EXPECT_EQ(print(Value::boolean(true)), "Bool(true)");
  EXPECT_EQ(print(Value::boolean(false)), "Bool(false)");
}

TEST(PrintValue, ColorHasAllEightDigitsAndItsFormat) {
  EXPECT_EQ(print(Value::colorValue(0x00ff0000, ColorFormat::Argb8)), "Color(0x00ff0000, Argb8)");
  EXPECT_EQ(print(Value::colorValue(0xffff8800, ColorFormat::Rgb4)), "Color(0xffff8800, Rgb4)");
}

TEST(PrintValue, Int) {
  EXPECT_EQ(print(Value::integerValue(-42)), "Int(-42)");
  EXPECT_EQ(print(Value::integerValue(16, true)), "Int(16, hex)");
}

TEST(PrintValue, FloatsOneUlpApartDiffer) {
  EXPECT_EQ(print(Value::floating(16777216)), "Float(16777216)");
  EXPECT_EQ(print(Value::floating(16777218)), "Float(16777218)");
  EXPECT_EQ(print(Value::floating(0.1f)), "Float(0.100000001)");
  EXPECT_EQ(print(Value::floating(-0.0f)), "Float(-0)");
}

TEST(PrintValue, DimensionAndFraction) {
  EXPECT_EQ(print(Value::dimensionFromComplex(0x1001)), "Dimension(16dp, 0x00001001)");
  EXPECT_EQ(print(*parseFloatValue("0.1dp")), "Dimension(0.100000024dp, 0x0ccccd31)");
  EXPECT_EQ(print(*parseFloatValue("50%p")), "Fraction(0.5, parent, 0x40000031)");
}

TEST(PrintValue, EveryKindShowsItsData) {
  Value ref;
  ref.kind = ValueKind::Reference;
  ref.ref = 0x7f010001;
  EXPECT_EQ(print(ref), "Reference(0x7f010001)");
  EXPECT_EQ(print(Value()), "Null()");
  EXPECT_EQ(print(withIndex(ValueKind::String, 3)), "String(index 3)");
  EXPECT_EQ(print(withIndex(ValueKind::StyleableDecl, 7)), "StyleableDecl(index 7)");
}

TEST(PrintValue, LeavesTheStreamAsItWas) {
  std::ostringstream os;
  os << std::hex << std::showpos;
  const auto flags = os.flags();
  os << Value::integerValue(16) << ' ' << 16;
  EXPECT_EQ(os.str(), "Int(16) 10");
  EXPECT_EQ(os.flags(), flags);
}

}  // namespace
}  // namespace hydroid::res

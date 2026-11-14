// Value's operator<<, which is also what gtest shows for a Value in failure messages.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

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

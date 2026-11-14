// Value's invariant (only factories set the kind, accessors check it), its operator== for the
// kinds the parser tests don't build, and its operator<<, which is also what gtest shows for a
// Value in failure messages.

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
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
    case ValueKind::Empty: return Value::empty();
    case ValueKind::Reference: return Value::reference(data);
    case ValueKind::AttrRef: return Value::attrRef(data);
    case ValueKind::Color: return Value::color(data, ColorFormat::Argb8);
    case ValueKind::Dimension: return Value::dimensionFromComplex(data);
    case ValueKind::Fraction: return Value::fractionFromComplex(data);
    case ValueKind::Float: return Value::floating(floatFromBits(data));
    case ValueKind::Int: return Value::integer(static_cast<int32_t>(data));
    case ValueKind::Bool: return Value::boolean(data != 0);
    case ValueKind::String: return Value::string(data);
    case ValueKind::StyledString: return Value::styledString(data);
    case ValueKind::File: return Value::file(data);
    case ValueKind::Style: return Value::style(data);
    case ValueKind::Array: return Value::array(data);
    case ValueKind::Plurals: return Value::plurals(data);
    case ValueKind::AttrDecl: return Value::attrDecl(data);
    case ValueKind::StyleableDecl: return Value::styleableDecl(data);
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

// ---- Invariant ------------------------------------------------------------------------------
// Detection traits: whether an expression on V compiles. V stays a template parameter so an
// ill-formed one is a substitution failure, not a hard error.

// Access is checked during substitution, so a private member is a substitution failure too. The
// names are Value.h's: renaming a member there means renaming it here, or these pass vacuously.
template <typename V, typename = void>
struct KindIsAccessible : std::false_type {};
template <typename V>
struct KindIsAccessible<V, std::void_t<decltype(std::declval<V&>().kind_)>> : std::true_type {};

template <typename V, typename = void>
struct UnionIsAccessible : std::false_type {};
template <typename V>
struct UnionIsAccessible<V, std::void_t<decltype(std::declval<V&>().color_)>> : std::true_type {};

template <typename V, typename T, typename = void>
struct IntegerTakesValue : std::false_type {};
template <typename V, typename T>
struct IntegerTakesValue<V, T, std::void_t<decltype(V::integer(std::declval<T>()))>>
    : std::true_type {};

template <typename V, typename T, typename = void>
struct BooleanTakes : std::false_type {};
template <typename V, typename T>
struct BooleanTakes<V, T, std::void_t<decltype(V::boolean(std::declval<T>()))>>
    : std::true_type {};

template <typename V, typename Format, typename = void>
struct IntegerTakes : std::false_type {};
template <typename V, typename Format>
struct IntegerTakes<V, Format, std::void_t<decltype(V::integer(0, std::declval<Format>()))>>
    : std::true_type {};

template <typename V, typename = void>
struct HasOldColorValue : std::false_type {};
template <typename V>
struct HasOldColorValue<V, std::void_t<decltype(V::colorValue(0u, ColorFormat::Rgb8))>>
    : std::true_type {};

template <typename V, typename = void>
struct HasOldIntegerValue : std::false_type {};
template <typename V>
struct HasOldIntegerValue<V, std::void_t<decltype(V::integerValue(0))>> : std::true_type {};

// v.kind = ValueKind::Dimension used to compile and leave the union holding something else.
static_assert(!KindIsAccessible<Value>::value);
static_assert(!UnionIsAccessible<Value>::value);
// integerValue(16, true) did not say what true meant.
static_assert(IntegerTakes<Value, IntFormat>::value);
static_assert(!IntegerTakes<Value, bool>::value);
static_assert(!IntegerTakes<Value, int>::value);
// integer(true) and boolean(42) would convert silently.
static_assert(IntegerTakesValue<Value, int32_t>::value);
static_assert(IntegerTakesValue<Value, uint32_t>::value);
static_assert(!IntegerTakesValue<Value, bool>::value);
static_assert(!IntegerTakesValue<Value, float>::value);
static_assert(!IntegerTakesValue<Value, double>::value);
static_assert(BooleanTakes<Value, bool>::value);
static_assert(!BooleanTakes<Value, int>::value);
static_assert(!BooleanTakes<Value, float>::value);
static_assert(!HasOldColorValue<Value>::value);
static_assert(!HasOldIntegerValue<Value>::value);
// Still a 32-bit payload plus tags, cheap to copy.
static_assert(std::is_trivially_copyable_v<Value>);

TEST(ValueFactory, EveryFactoryMakesItsKind) {
  EXPECT_EQ(Value().kind(), ValueKind::Null);
  for (ValueKind kind : everyKind()) {
    EXPECT_EQ(sample(kind, 1)->kind(), kind) << testing::PrintToString(*sample(kind, 1));
  }
}

TEST(ValueFactory, AccessorsReturnWhatTheFactoryTook) {
  EXPECT_EQ(Value::reference(0x7f010001).ref(), 0x7f010001u);
  EXPECT_EQ(Value::attrRef(0x01010000).ref(), 0x01010000u);
  const Value color = Value::color(0x80ff8800, ColorFormat::Argb4);
  EXPECT_EQ(color.argb(), 0x80ff8800u);
  EXPECT_EQ(color.colorFormat(), ColorFormat::Argb4);
  EXPECT_EQ(Value::integer(-42).intValue(), -42);
  EXPECT_EQ(Value::integer(-42).intFormat(), IntFormat::Dec);
  EXPECT_EQ(Value::integer(16, IntFormat::Hex).intFormat(), IntFormat::Hex);
  EXPECT_TRUE(Value::boolean(true).boolValue());
  EXPECT_FALSE(Value::boolean(false).boolValue());
  EXPECT_EQ(Value::floating(1.5f).floatValue(), 1.5f);
  EXPECT_EQ(Value::fractionFromComplex(0x40000031).complexData(), 0x40000031u);
  EXPECT_EQ(Value::string(3).index(), 3u);
  EXPECT_EQ(Value::styleableDecl(7).index(), 7u);
}

// Only a debug build of the library checks; in release the accessors read the union unchecked.
// The tests are built with the library, so their NDEBUG is the library's.
#ifndef NDEBUG
TEST(ValueAccessorDeathTest, WrongKindStopsADebugBuild) {
  EXPECT_DEATH(Value().argb(), "not a Color");
  EXPECT_DEATH(Value::dimensionFromComplex(0x1001).colorFormat(), "not a Color");
  EXPECT_DEATH(Value::floating(1).intValue(), "not an Int");
  EXPECT_DEATH(Value::color(0, ColorFormat::Rgb8).intFormat(), "not an Int");
  EXPECT_DEATH(Value::integer(1).boolValue(), "not a Bool");
  EXPECT_DEATH(Value::integer(1).floatValue(), "not a Float");
  EXPECT_DEATH(Value::floating(16).complexData(), "not a Dimension or Fraction");
  EXPECT_DEATH(Value::string(0).ref(), "not a reference");
  EXPECT_DEATH(Value::reference(0x7f010001).index(), "not a pooled kind");
  EXPECT_DEATH(Value().index(), "not a pooled kind");
  EXPECT_DEATH(Value::empty().ref(), "not a reference");
}
#endif

// Value is trivially copyable, so bytes can hold a kind no factory makes.
TEST(ValueBytes, KindOutOfRangeHoldsNothing) {
  Value v = Value::integer(1);
  const auto kind = static_cast<uint8_t>(0x40);
  static_assert(sizeof(ValueKind) == sizeof kind);
  std::memcpy(static_cast<void*>(&v), &kind, sizeof kind);  // kind_ is the first member
  ASSERT_EQ(static_cast<int>(v.kind()), 0x40);
  EXPECT_EQ(v, v);
  EXPECT_NE(v, Value());
  EXPECT_EQ(print(v), "?()");
}

// ---- operator== -----------------------------------------------------------------------------

TEST(ValueEquality, EveryKindComparesItsData) {
  for (ValueKind kind : everyKind()) {
    SCOPED_TRACE(testing::PrintToString(*sample(kind, 1)));
    EXPECT_EQ(sample(kind, 1), sample(kind, 1));
    if (kind != ValueKind::Null && kind != ValueKind::Empty) {
      EXPECT_NE(sample(kind, 1), sample(kind, 0));
    }
  }
}

// Every kind but Null and Empty holds data 1 in the same union bits, so only the kind tells them
// apart.
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
  const Value ref = Value::reference(0x7f010001);
  const Value attr = Value::attrRef(0x7f010001);
  const Value other = Value::reference(0x7f010002);
  EXPECT_EQ(ref, ref);
  EXPECT_NE(ref, other);
  EXPECT_NE(ref, attr);
  EXPECT_EQ(Value(), Value());
}

// @null and @empty both hold no data, but mean different things at runtime (#18).
TEST(ValueEquality, NullIsNotEmpty) {
  EXPECT_EQ(Value::empty(), Value::empty());
  EXPECT_NE(Value(), Value::empty());
}

TEST(ValueEquality, FloatComparesBits) {
  EXPECT_EQ(Value::floating(1.5f), Value::floating(1.5f));
  EXPECT_NE(Value::floating(-0.0f), Value::floating(0.0f));
  const Value nan = Value::floating(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(nan, nan);
}

// ---- operator<< -----------------------------------------------------------------------------

TEST(PrintValue, IsWhatGtestShows) {
  EXPECT_EQ(testing::PrintToString(Value::boolean(true)), "Bool(true)");
}

TEST(PrintValue, Bool) {
  EXPECT_EQ(print(Value::boolean(true)), "Bool(true)");
  EXPECT_EQ(print(Value::boolean(false)), "Bool(false)");
}

TEST(PrintValue, ColorHasAllEightDigitsAndItsFormat) {
  EXPECT_EQ(print(Value::color(0x00ff0000, ColorFormat::Argb8)), "Color(0x00ff0000, Argb8)");
  EXPECT_EQ(print(Value::color(0xffff8800, ColorFormat::Rgb4)), "Color(0xffff8800, Rgb4)");
}

TEST(PrintValue, Int) {
  EXPECT_EQ(print(Value::integer(-42)), "Int(-42)");
  EXPECT_EQ(print(Value::integer(16, IntFormat::Hex)), "Int(16, hex)");
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
  EXPECT_EQ(print(Value::reference(0x7f010001)), "Reference(0x7f010001)");
  EXPECT_EQ(print(Value::attrRef(0x01010000)), "AttrRef(0x01010000)");
  EXPECT_EQ(print(Value()), "Null()");
  EXPECT_EQ(print(Value::empty()), "Empty()");
  EXPECT_EQ(print(Value::string(3)), "String(index 3)");
  EXPECT_EQ(print(Value::styleableDecl(7)), "StyleableDecl(index 7)");
}

TEST(PrintValue, LeavesTheStreamAsItWas) {
  std::ostringstream os;
  os << std::hex << std::showpos;
  const auto flags = os.flags();
  os << Value::integer(16) << ' ' << 16;
  EXPECT_EQ(os.str(), "Int(16) 10");
  EXPECT_EQ(os.flags(), flags);
}

}  // namespace
}  // namespace hydroid::res

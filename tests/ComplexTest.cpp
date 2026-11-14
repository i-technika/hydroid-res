// Android's complex dimension/fraction encoding: aapt2's packing and TypedValue's unpacking.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include "ValuePrinter.h"
#include "hydroid/res/Complex.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

constexpr uint32_t kDp = static_cast<uint32_t>(DimUnit::Dp);

// ---- Value factories -----------------------------------------------------------------------
// Detection traits: whether V::f(Arg) compiles. V stays a template parameter so a missing or
// deleted function is a substitution failure, not a hard error.

template <typename V, typename Arg, typename = void>
struct AcceptsDimensionFromComplex : std::false_type {};
template <typename V, typename Arg>
struct AcceptsDimensionFromComplex<
    V, Arg, std::void_t<decltype(V::dimensionFromComplex(std::declval<Arg>()))>>
    : std::true_type {};

template <typename V, typename Arg, typename = void>
struct AcceptsFractionFromComplex : std::false_type {};
template <typename V, typename Arg>
struct AcceptsFractionFromComplex<
    V, Arg, std::void_t<decltype(V::fractionFromComplex(std::declval<Arg>()))>>
    : std::true_type {};

template <typename V, typename = void>
struct HasOldDimension : std::false_type {};
template <typename V>
struct HasOldDimension<V, std::void_t<decltype(V::dimension(std::declval<int>()))>>
    : std::true_type {};

template <typename V, typename = void>
struct HasOldFractionValue : std::false_type {};
template <typename V>
struct HasOldFractionValue<V, std::void_t<decltype(V::fractionValue(std::declval<int>()))>>
    : std::true_type {};

// Value::dimension(16) used to compile and store data 0x10, which is 0px, not 16px.
static_assert(!HasOldDimension<Value>::value);
static_assert(!HasOldFractionValue<Value>::value);

// Complex data is an integer; a float there is a number mistaken for data.
static_assert(AcceptsDimensionFromComplex<Value, uint32_t>::value);
static_assert(AcceptsDimensionFromComplex<Value, int>::value);  // hex literals: 0x1001
static_assert(!AcceptsDimensionFromComplex<Value, float>::value);
static_assert(!AcceptsDimensionFromComplex<Value, double>::value);
static_assert(AcceptsFractionFromComplex<Value, uint32_t>::value);
static_assert(!AcceptsFractionFromComplex<Value, float>::value);
static_assert(!AcceptsFractionFromComplex<Value, double>::value);

TEST(ValueFromComplex, KeepsDataAndKind) {
  const Value dim = Value::dimensionFromComplex(0x1001);
  EXPECT_EQ(dim.kind, ValueKind::Dimension);
  EXPECT_EQ(dim.complex, 0x1001u);
  const Value frac = Value::fractionFromComplex(0x40000031);
  EXPECT_EQ(frac.kind, ValueKind::Fraction);
  EXPECT_EQ(frac.complex, 0x40000031u);
  // Same data, different kind.
  EXPECT_NE(Value::dimensionFromComplex(0x100), Value::fractionFromComplex(0x100));
}

TEST(ValueFromComplex, NumberGoesThroughFloatToComplex) {
  // How to build 16dp from a number: encode first, as the parser does.
  EXPECT_EQ(Value::dimensionFromComplex(*floatToComplex(16, kDp)), parseFloatValue("16dp"));
  EXPECT_EQ(Value::fractionFromComplex(*floatToComplex(0.5f, kComplexUnitFractionParent)),
            parseFloatValue("50%p"));
  // The raw number is not the value: data 16 is 0px.
  EXPECT_EQ(complexToDimension(Value::dimensionFromComplex(16).complex,
                               DisplayMetrics::forDensity(160)),
            0.0f);
}

TEST(ValueFromComplex, KeepsUnknownUnitsLikeAndroid) {
  // Units are not validated: Android keeps them too, and complexToDimension gives 0.
  EXPECT_EQ(Value::dimensionFromComplex(0x100f).complex, 0x100fu);
  EXPECT_EQ(complexToDimension(0x100f, DisplayMetrics::forDensity(160)), 0.0f);
}

// ---- floatToComplex ------------------------------------------------------------------------

TEST(FloatToComplex, PicksTheNarrowestRadix) {
  EXPECT_EQ(floatToComplex(16, kDp), 0x1001u);            // 23p0: no fraction
  EXPECT_EQ(floatToComplex(0.5f, kDp), 0x40000031u);      // 0p23: |v| < 1
  EXPECT_EQ(floatToComplex(1.5f, kDp), 0x00c00021u);      // 8p15: |v| < 256
  EXPECT_EQ(floatToComplex(1234.5678f, kDp), 0x02694811u);  // 16p7: |v| < 65536
  EXPECT_EQ(floatToComplex(300000.5f, kDp), 0x0493e001u);   // 23p0: fraction dropped
}

TEST(FloatToComplex, Negative) {
  EXPECT_EQ(floatToComplex(-4, kDp), 0xfffffc01u);
  EXPECT_EQ(floatToComplex(-1.5f, kDp), 0xff400021u);
  EXPECT_EQ(floatToComplex(-0.0f, kDp), kDp);
}

TEST(FloatToComplex, RoundsToNearestInFloat) {
  // value * 2^23 + .5f, truncated: 2^-24 rounds up, 2^-25 rounds to zero.
  EXPECT_EQ(floatToComplex(0x1p-24f, kDp), 0x00000131u);
  EXPECT_EQ(floatToComplex(0x3p-24f, kDp), 0x00000231u);
  EXPECT_EQ(floatToComplex(0x1p-25f, kDp), kDp);
  // 0.1 * 2^23 = 838860.8 → 838861.
  EXPECT_EQ(floatToComplex(0.1f, kDp), 0x0ccccd31u);
}

TEST(FloatToComplex, MantissaWrapsLikeAapt) {
  EXPECT_EQ(floatToComplex(8388607, kDp), 0x7fffff01u);
  EXPECT_EQ(floatToComplex(8388608, kDp), 0x80000001u);  // reads back as -8388608
  EXPECT_EQ(floatToComplex(9000000, kDp), 0x89544001u);  // reads back as -7777216
  // Largest value aapt2 encodes without overflowing its 64-bit intermediate.
  EXPECT_EQ(floatToComplex(std::nextafter(0x1p41f, 0.0f), kDp), 0xfe000001u);
}

TEST(FloatToComplex, KeepsUnitBits) {
  EXPECT_EQ(floatToComplex(1, kComplexUnitFractionParent), 0x101u);
  EXPECT_EQ(floatToComplex(1, static_cast<uint32_t>(DimUnit::Mm)), 0x105u);
}

TEST(FloatToComplex, RejectsWhereAaptIsUndefined) {
  EXPECT_FALSE(floatToComplex(0x1p41f, kDp));
  EXPECT_FALSE(floatToComplex(-0x1p41f, kDp));
  EXPECT_FALSE(floatToComplex(std::numeric_limits<float>::infinity(), kDp));
  EXPECT_FALSE(floatToComplex(std::numeric_limits<float>::quiet_NaN(), kDp));
}

// ---- complexToFloat ------------------------------------------------------------------------

TEST(ComplexToFloat, EachRadix) {
  EXPECT_EQ(complexToFloat(0x00000100), 1.0f);
  EXPECT_EQ(complexToFloat(0x00000110), 0x1p-7f);
  EXPECT_EQ(complexToFloat(0x00000120), 0x1p-15f);
  EXPECT_EQ(complexToFloat(0x00000130), 0x1p-23f);
  EXPECT_EQ(complexToFloat(0xffffff00), -1.0f);
  EXPECT_EQ(complexToFloat(0x80000000), -8388608.0f);
}

TEST(ComplexToFloat, IgnoresUnit) {
  EXPECT_EQ(complexToFloat(0x1001), 16.0f);
  EXPECT_EQ(complexToFloat(0x100f), 16.0f);
}

TEST(ComplexToFloat, Issue3Table) {
  EXPECT_EQ(complexToFloat(*floatToComplex(0.1f, kDp)), 838861 * 0x1p-23f);  // 0.100000024
  EXPECT_EQ(complexToFloat(*floatToComplex(1234.5678f, kDp)), 1234.5625f);
  EXPECT_EQ(complexToFloat(*floatToComplex(9000000, kDp)), -7777216.0f);
}

// ---- complexToDimension / complexToFraction ------------------------------------------------

TEST(DisplayMetrics, ForDensity) {
  DisplayMetrics m = DisplayMetrics::forDensity(213);
  EXPECT_EQ(m.density, 213 * (1.0f / 160));
  EXPECT_EQ(m.scaledDensity, m.density);
  EXPECT_EQ(m.xdpi, 213.0f);
  EXPECT_EQ(DisplayMetrics::forDensity(320, 1.5f).scaledDensity, 3.0f);
}

TEST(DisplayMetrics, ZeroFontScaleMeansOne) {
  // ResourcesImpl: scaledDensity = density * (fontScale != 0 ? fontScale : 1.0f).
  EXPECT_EQ(DisplayMetrics::forDensity(320, 0).scaledDensity, 2.0f);
  EXPECT_EQ(DisplayMetrics::forDensity(320, -0.0f).scaledDensity, 2.0f);
  EXPECT_EQ(complexToDimension(0xe02, DisplayMetrics::forDensity(320, 0)), 28.0f);  // 14sp
}

TEST(ComplexToDimension, Units) {
  const DisplayMetrics m = DisplayMetrics::forDensity(320, 1.5f);
  EXPECT_EQ(complexToDimension(0x100, m), 1.0f);     // 1px
  EXPECT_EQ(complexToDimension(0x1001, m), 32.0f);   // 16dp × 2
  EXPECT_EQ(complexToDimension(0xe02, m), 42.0f);    // 14sp × 3
  // pt and mm in TypedValue.applyDimension's order: value * xdpi * (1.0f/72). For 5pt and 7mm
  // at xdpi 320, value * xdpi / 72 (or / 25.4f) differs in the last bit.
  EXPECT_EQ(complexToDimension(0x503, m), 5.0f * 320.0f * (1.0f / 72));     // 5pt
  EXPECT_NE(complexToDimension(0x503, m), 5.0f * 320.0f / 72);
  EXPECT_EQ(complexToDimension(0x104, m), 320.0f);                          // 1in
  EXPECT_EQ(complexToDimension(0x705, m), 7.0f * 320.0f * (1.0f / 25.4f));  // 7mm
  EXPECT_NE(complexToDimension(0x705, m), 7.0f * 320.0f / 25.4f);
  EXPECT_EQ(complexToDimension(0x106, m), 0.0f);     // unknown unit
}

TEST(ComplexToFraction, BaseAndParentBase) {
  EXPECT_EQ(complexToFraction(0x40000030, 200, 1000), 100.0f);  // 50%
  EXPECT_EQ(complexToFraction(0x40000031, 200, 1000), 500.0f);  // 50%p
  EXPECT_EQ(complexToFraction(0x40000032, 200, 1000), 0.0f);    // not a fraction unit
}

}  // namespace
}  // namespace hydroid::res

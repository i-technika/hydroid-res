// Step 2 completion check: color, dimen, integer, bool, fraction text → Value, as aapt2 parses it.

#include <gtest/gtest.h>

#include <ostream>

#include "hydroid/res/Complex.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {

void PrintTo(const Value& v, std::ostream* os) {
  switch (v.kind) {
    case ValueKind::Color:
      *os << "Color(0x" << std::hex << v.color.argb << std::dec << ", type "
          << static_cast<int>(v.color.format) << ")";
      break;
    case ValueKind::Int: *os << "Int(" << v.integer.v << (v.integer.hex ? ", hex)" : ")"); break;
    case ValueKind::Bool: *os << "Bool(" << v.b << ")"; break;
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

namespace {

// ---- color ---------------------------------------------------------------------------------

TEST(ParseColor, FourFormats) {
  EXPECT_EQ(parseColor("#f80"), Value::colorValue(0xffff8800, ColorFormat::Rgb4));
  EXPECT_EQ(parseColor("#8f80"), Value::colorValue(0x88ff8800, ColorFormat::Argb4));
  EXPECT_EQ(parseColor("#6200ee"), Value::colorValue(0xff6200ee, ColorFormat::Rgb8));
  EXPECT_EQ(parseColor("#806200ee"), Value::colorValue(0x806200ee, ColorFormat::Argb8));
}

TEST(ParseColor, FormatIsTheResValueType) {
  // Issue #4: the golden app has accent (#6200EE) as type 29 and surface (#FFFFFFFF) as 28.
  EXPECT_EQ(static_cast<int>(ColorFormat::Argb8), 28);  // TYPE_INT_COLOR_ARGB8
  EXPECT_EQ(static_cast<int>(ColorFormat::Rgb8), 29);   // TYPE_INT_COLOR_RGB8
  EXPECT_EQ(static_cast<int>(ColorFormat::Argb4), 30);  // TYPE_INT_COLOR_ARGB4
  EXPECT_EQ(static_cast<int>(ColorFormat::Rgb4), 31);   // TYPE_INT_COLOR_RGB4
}

TEST(ParseColor, SameColorInAnotherFormatIsAnotherValue) {
  // Opaque white four ways: equal argb, but Android keeps four different types.
  EXPECT_EQ(parseColor("#fff"), Value::colorValue(0xffffffff, ColorFormat::Rgb4));
  EXPECT_EQ(parseColor("#ffff"), Value::colorValue(0xffffffff, ColorFormat::Argb4));
  EXPECT_EQ(parseColor("#ffffff"), Value::colorValue(0xffffffff, ColorFormat::Rgb8));
  EXPECT_EQ(parseColor("#ffffffff"), Value::colorValue(0xffffffff, ColorFormat::Argb8));
  EXPECT_NE(parseColor("#fff"), parseColor("#ffffffff"));
  EXPECT_NE(parseColor("#ffffff"), parseColor("#ffffffff"));
}

TEST(ParseColor, HexIsCaseInsensitiveAndWhitespaceIsTrimmed) {
  EXPECT_EQ(parseColor("#FF6200EE"), Value::colorValue(0xff6200ee, ColorFormat::Argb8));
  EXPECT_EQ(parseColor("#aBcDeF"), Value::colorValue(0xffabcdef, ColorFormat::Rgb8));
  EXPECT_EQ(parseColor("  #fff\n"), Value::colorValue(0xffffffff, ColorFormat::Rgb4));
}

TEST(ParseColor, ZeroAlphaIsKept) {
  EXPECT_EQ(parseColor("#0000"), Value::colorValue(0x00000000, ColorFormat::Argb4));
  EXPECT_EQ(parseColor("#00ffffff"), Value::colorValue(0x00ffffff, ColorFormat::Argb8));
}

TEST(ParseColor, Rejects) {
  for (const char* s : {"", "#", "#f", "#ff", "#fffff", "#fffffff", "#fffffffff", "fff",
                        "ffffff", "#ggg", "# fff", "#ff ff", "0xffffff", "@color/x"}) {
    EXPECT_FALSE(parseColor(s)) << '"' << s << '"';
  }
}

// ---- dimension -----------------------------------------------------------------------------
// Expected values are Res_value data as aapt2 writes it: mantissa << 8 | radix << 4 | unit.

TEST(ParseFloatValue, SixUnits) {
  EXPECT_EQ(parseFloatValue("1px"), Value::dimensionFromComplex(0x100));
  EXPECT_EQ(parseFloatValue("16dp"), Value::dimensionFromComplex(0x1001));
  EXPECT_EQ(parseFloatValue("14sp"), Value::dimensionFromComplex(0xe02));
  EXPECT_EQ(parseFloatValue("12pt"), Value::dimensionFromComplex(0xc03));
  EXPECT_EQ(parseFloatValue("2in"), Value::dimensionFromComplex(0x204));
  EXPECT_EQ(parseFloatValue("25mm"), Value::dimensionFromComplex(0x1905));
}

TEST(ParseFloatValue, DipIsDp) {
  EXPECT_EQ(parseFloatValue("8dip"), Value::dimensionFromComplex(0x801));
}

TEST(ParseFloatValue, NumberForms) {
  EXPECT_EQ(parseFloatValue("-4dp"), Value::dimensionFromComplex(0xfffffc01));
  EXPECT_EQ(parseFloatValue("+4dp"), Value::dimensionFromComplex(0x401));
  EXPECT_EQ(parseFloatValue("0.5dp"), Value::dimensionFromComplex(0x40000031));
  EXPECT_EQ(parseFloatValue(".5dp"), Value::dimensionFromComplex(0x40000031));
  EXPECT_EQ(parseFloatValue("5.dp"), Value::dimensionFromComplex(0x501));
  EXPECT_EQ(parseFloatValue("1.5e2px"), Value::dimensionFromComplex(0x9600));
  EXPECT_EQ(parseFloatValue("\t16dp \n"), Value::dimensionFromComplex(0x1001));
}

TEST(ParseFloatValue, KeepsAaptPrecision) {
  // Issue #3: Android reads these back as 0.100000024, 1234.5625 and -7777216.
  // radix 0p23, radix 16p7, mantissa wraps
  EXPECT_EQ(parseFloatValue("0.1dp"), Value::dimensionFromComplex(0x0ccccd31));
  EXPECT_EQ(parseFloatValue("1234.5678dp"), Value::dimensionFromComplex(0x02694811));
  EXPECT_EQ(parseFloatValue("9000000dp"), Value::dimensionFromComplex(0x89544001));
}

TEST(ParseFloatValue, NoUnitIsFloat) {
  EXPECT_EQ(parseFloatValue("1.5"), Value::floating(1.5f));
  EXPECT_EQ(parseFloatValue("-2"), Value::floating(-2));
}

TEST(ParseFloatValue, Fractions) {
  EXPECT_EQ(parseFloatValue("50%"), Value::fractionFromComplex(0x40000030));
  EXPECT_EQ(parseFloatValue("50%p"), Value::fractionFromComplex(0x40000031));
  EXPECT_EQ(parseFloatValue("-25%"), Value::fractionFromComplex(0xe0000030));
  EXPECT_EQ(parseFloatValue("100%"), Value::fractionFromComplex(0x100));
}

TEST(ParseFloatValue, FractionScalesInFloatLikeAapt) {
  // aapt2 does f *= 1.0f/100 in float; 30 / 100.0 in double would give 0x26666730.
  EXPECT_EQ(parseFloatValue("30%"), Value::fractionFromComplex(0x26666630));
  EXPECT_EQ(parseFloatValue("18%p"), Value::fractionFromComplex(0x170a3d31));
}

TEST(ParseFloatValue, Rejects) {
  // 3e12dp and up overflow aapt2's 64-bit intermediate (undefined there).
  for (const char* s : {"", "dp", "16 dp", "16DP", "16Dp", "16dpx", "16em", "16px5", ".dp",
                        "-dp", "1e", "1e+dp", "16%%", "1,5dp", "1e39dp", "1e39%", "3e12dp",
                        "inf", "nan"}) {
    EXPECT_FALSE(parseFloatValue(s)) << '"' << s << '"';
  }
}

// ---- integer -------------------------------------------------------------------------------

TEST(ParseInteger, Decimal) {
  EXPECT_EQ(parseInteger("0"), Value::integerValue(0));
  EXPECT_EQ(parseInteger("42"), Value::integerValue(42));
  EXPECT_EQ(parseInteger("-42"), Value::integerValue(-42));
  EXPECT_EQ(parseInteger(" 7\n"), Value::integerValue(7));
  EXPECT_EQ(parseInteger("2147483647"), Value::integerValue(INT32_MAX));
  EXPECT_EQ(parseInteger("-2147483648"), Value::integerValue(INT32_MIN));
}

TEST(ParseInteger, Hex) {
  EXPECT_EQ(parseInteger("0x10"), Value::integerValue(16, true));
  EXPECT_EQ(parseInteger("0x7f010000"), Value::integerValue(0x7f010000, true));
  EXPECT_EQ(parseInteger("0xFfFfFfFf"), Value::integerValue(-1, true));
}

TEST(ParseInteger, Rejects) {
  for (const char* s : {"", "-", "+5", "2147483648", "-2147483649", "0x", "0X10", "-0x10",
                        "0x100000000", "0xg", "4 2", "1.0", "1e3", "x10", "true"}) {
    EXPECT_FALSE(parseInteger(s)) << '"' << s << '"';
  }
}

// ---- bool ----------------------------------------------------------------------------------

TEST(ParseBool, AcceptedSpellings) {
  for (const char* s : {"true", "True", "TRUE", " true\n"}) {
    EXPECT_EQ(parseBool(s), Value::boolean(true)) << s;
  }
  for (const char* s : {"false", "False", "FALSE"}) {
    EXPECT_EQ(parseBool(s), Value::boolean(false)) << s;
  }
}

TEST(ParseBool, Rejects) {
  for (const char* s : {"", "tRUE", "yes", "1", "0", "truee"}) {
    EXPECT_FALSE(parseBool(s)) << '"' << s << '"';
  }
}

// ---- format masks --------------------------------------------------------------------------

constexpr uint32_t kDimenTag = kFormatFloat | kFormatFraction | kFormatDimension;  // <dimen>

TEST(ParseLiteral, DimenTagTakesFloatDimensionAndFraction) {
  EXPECT_EQ(parseLiteral("16dp", kDimenTag), Value::dimensionFromComplex(0x1001));
  EXPECT_EQ(parseLiteral("1.5", kDimenTag), Value::floating(1.5f));
  EXPECT_EQ(parseLiteral("5", kDimenTag), Value::floating(5));
  EXPECT_EQ(parseLiteral("50%", kDimenTag), Value::fractionFromComplex(0x40000030));
  EXPECT_FALSE(parseLiteral("#fff", kDimenTag));
}

TEST(ParseLiteral, MaskFiltersFloatKinds) {
  EXPECT_FALSE(parseLiteral("16dp", kFormatFloat));
  EXPECT_FALSE(parseLiteral("1.5", kFormatDimension));
  EXPECT_EQ(parseLiteral("1.5", kFormatFloat), Value::floating(1.5f));
}

TEST(ParseLiteral, AaptOrder) {
  // Integer is tried before float, color before everything.
  EXPECT_EQ(parseLiteral("5", kFormatInteger | kFormatFloat), Value::integerValue(5));
  EXPECT_EQ(parseLiteral("#fff", kFormatAny), Value::colorValue(0xffffffff, ColorFormat::Rgb4));
  EXPECT_EQ(parseLiteral("true", kFormatAny), Value::boolean(true));
  EXPECT_FALSE(parseLiteral("hello", kFormatAny));
}

}  // namespace
}  // namespace hydroid::res

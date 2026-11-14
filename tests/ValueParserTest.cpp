// Step 2 completion check: color, dimen, integer, bool, fraction text → Value, as aapt2 parses it.

#include <gtest/gtest.h>

#include <ostream>

#include "hydroid/res/ValueParser.h"

namespace hydroid::res {

void PrintTo(const Value& v, std::ostream* os) {
  switch (v.kind) {
    case ValueKind::Color: *os << "Color(0x" << std::hex << v.argb << std::dec << ")"; break;
    case ValueKind::Int: *os << "Int(" << v.integer.v << (v.integer.hex ? ", hex)" : ")"); break;
    case ValueKind::Bool: *os << "Bool(" << v.b << ")"; break;
    case ValueKind::Dimension: *os << "Dimension(" << v.dim.v << toString(v.dim.unit) << ")"; break;
    case ValueKind::Fraction:
      *os << "Fraction(" << v.fraction.v << (v.fraction.parent ? ", parent)" : ")");
      break;
    case ValueKind::Float: *os << "Float(" << v.f << ")"; break;
    default: *os << "Value(kind " << static_cast<int>(v.kind) << ")"; break;
  }
}

namespace {

// ---- color ---------------------------------------------------------------------------------

TEST(ParseColor, FourFormats) {
  EXPECT_EQ(parseColor("#f80"), Value::color(0xffff8800));
  EXPECT_EQ(parseColor("#8f80"), Value::color(0x88ff8800));
  EXPECT_EQ(parseColor("#6200ee"), Value::color(0xff6200ee));
  EXPECT_EQ(parseColor("#806200ee"), Value::color(0x806200ee));
}

TEST(ParseColor, HexIsCaseInsensitiveAndWhitespaceIsTrimmed) {
  EXPECT_EQ(parseColor("#FF6200EE"), Value::color(0xff6200ee));
  EXPECT_EQ(parseColor("#aBcDeF"), Value::color(0xffabcdef));
  EXPECT_EQ(parseColor("  #fff\n"), Value::color(0xffffffff));
}

TEST(ParseColor, ZeroAlphaIsKept) {
  EXPECT_EQ(parseColor("#0000"), Value::color(0x00000000));
  EXPECT_EQ(parseColor("#00ffffff"), Value::color(0x00ffffff));
}

TEST(ParseColor, Rejects) {
  for (const char* s : {"", "#", "#f", "#ff", "#fffff", "#fffffff", "#fffffffff", "fff",
                        "ffffff", "#ggg", "# fff", "#ff ff", "0xffffff", "@color/x"}) {
    EXPECT_FALSE(parseColor(s)) << '"' << s << '"';
  }
}

// ---- dimension -----------------------------------------------------------------------------

TEST(ParseFloatValue, SixUnits) {
  EXPECT_EQ(parseFloatValue("1px"), Value::dimension(1, DimUnit::Px));
  EXPECT_EQ(parseFloatValue("16dp"), Value::dimension(16, DimUnit::Dp));
  EXPECT_EQ(parseFloatValue("14sp"), Value::dimension(14, DimUnit::Sp));
  EXPECT_EQ(parseFloatValue("12pt"), Value::dimension(12, DimUnit::Pt));
  EXPECT_EQ(parseFloatValue("2in"), Value::dimension(2, DimUnit::In));
  EXPECT_EQ(parseFloatValue("25mm"), Value::dimension(25, DimUnit::Mm));
}

TEST(ParseFloatValue, DipIsDp) {
  EXPECT_EQ(parseFloatValue("8dip"), Value::dimension(8, DimUnit::Dp));
}

TEST(ParseFloatValue, NumberForms) {
  EXPECT_EQ(parseFloatValue("-4dp"), Value::dimension(-4, DimUnit::Dp));
  EXPECT_EQ(parseFloatValue("+4dp"), Value::dimension(4, DimUnit::Dp));
  EXPECT_EQ(parseFloatValue("0.5dp"), Value::dimension(0.5f, DimUnit::Dp));
  EXPECT_EQ(parseFloatValue(".5dp"), Value::dimension(0.5f, DimUnit::Dp));
  EXPECT_EQ(parseFloatValue("5.dp"), Value::dimension(5, DimUnit::Dp));
  EXPECT_EQ(parseFloatValue("1.5e2px"), Value::dimension(150, DimUnit::Px));
  EXPECT_EQ(parseFloatValue("\t16dp \n"), Value::dimension(16, DimUnit::Dp));
}

TEST(ParseFloatValue, NoUnitIsFloat) {
  EXPECT_EQ(parseFloatValue("1.5"), Value::floating(1.5f));
  EXPECT_EQ(parseFloatValue("-2"), Value::floating(-2));
}

TEST(ParseFloatValue, Fractions) {
  EXPECT_EQ(parseFloatValue("50%"), Value::fractionValue(0.5f, false));
  EXPECT_EQ(parseFloatValue("50%p"), Value::fractionValue(0.5f, true));
  EXPECT_EQ(parseFloatValue("-25%"), Value::fractionValue(-0.25f, false));
}

TEST(ParseFloatValue, Rejects) {
  for (const char* s : {"", "dp", "16 dp", "16DP", "16Dp", "16dpx", "16em", "16px5", ".dp",
                        "-dp", "1e", "1e+dp", "16%%", "1,5dp", "1e39dp", "inf", "nan"}) {
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
  EXPECT_EQ(parseLiteral("16dp", kDimenTag), Value::dimension(16, DimUnit::Dp));
  EXPECT_EQ(parseLiteral("1.5", kDimenTag), Value::floating(1.5f));
  EXPECT_EQ(parseLiteral("5", kDimenTag), Value::floating(5));
  EXPECT_EQ(parseLiteral("50%", kDimenTag), Value::fractionValue(0.5f, false));
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
  EXPECT_EQ(parseLiteral("#fff", kFormatAny), Value::color(0xffffffff));
  EXPECT_EQ(parseLiteral("true", kFormatAny), Value::boolean(true));
  EXPECT_FALSE(parseLiteral("hello", kFormatAny));
}

}  // namespace
}  // namespace hydroid::res

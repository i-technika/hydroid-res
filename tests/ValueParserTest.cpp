// Step 2 completion check: color, dimen, integer, bool, fraction text → Value, as aapt2 parses it.
// Aapt2ValueTest checks the same parsers against aapt2's own output (golden/aapt2/values.tsv);
// these tests name the cases. Where hydroid rejects on purpose what aapt2 takes, the test says so.

#include <gtest/gtest.h>

#include <cfloat>
#include <string>
#include <string_view>

#include "hydroid/res/Complex.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

using namespace std::string_view_literals;

// ---- color ---------------------------------------------------------------------------------

TEST(ParseColor, FourFormats) {
  EXPECT_EQ(parseColor("#f80"), Value::color(0xffff8800, ColorFormat::Rgb4));
  EXPECT_EQ(parseColor("#8f80"), Value::color(0x88ff8800, ColorFormat::Argb4));
  EXPECT_EQ(parseColor("#6200ee"), Value::color(0xff6200ee, ColorFormat::Rgb8));
  EXPECT_EQ(parseColor("#806200ee"), Value::color(0x806200ee, ColorFormat::Argb8));
}

// Issue #4: the golden app has accent (#6200EE) as type 29 and surface (#FFFFFFFF) as 28.
static_assert(static_cast<int>(ColorFormat::Argb8) == 28);  // TYPE_INT_COLOR_ARGB8
static_assert(static_cast<int>(ColorFormat::Rgb8) == 29);   // TYPE_INT_COLOR_RGB8
static_assert(static_cast<int>(ColorFormat::Argb4) == 30);  // TYPE_INT_COLOR_ARGB4
static_assert(static_cast<int>(ColorFormat::Rgb4) == 31);   // TYPE_INT_COLOR_RGB4

TEST(ParseColor, SameColorInAnotherFormatIsAnotherValue) {
  // Opaque white four ways: equal argb, but Android keeps four different types.
  EXPECT_EQ(parseColor("#fff"), Value::color(0xffffffff, ColorFormat::Rgb4));
  EXPECT_EQ(parseColor("#ffff"), Value::color(0xffffffff, ColorFormat::Argb4));
  EXPECT_EQ(parseColor("#ffffff"), Value::color(0xffffffff, ColorFormat::Rgb8));
  EXPECT_EQ(parseColor("#ffffffff"), Value::color(0xffffffff, ColorFormat::Argb8));
  EXPECT_NE(parseColor("#fff"), parseColor("#ffffffff"));
  EXPECT_NE(parseColor("#ffffff"), parseColor("#ffffffff"));
}

TEST(ParseColor, HexIsCaseInsensitiveAndWhitespaceIsTrimmed) {
  EXPECT_EQ(parseColor("#FF6200EE"), Value::color(0xff6200ee, ColorFormat::Argb8));
  EXPECT_EQ(parseColor("#aBcDeF"), Value::color(0xffabcdef, ColorFormat::Rgb8));
  EXPECT_EQ(parseColor("  #fff\n"), Value::color(0xffffffff, ColorFormat::Rgb4));
}

TEST(ParseColor, ZeroAlphaIsKept) {
  EXPECT_EQ(parseColor("#0000"), Value::color(0x00000000, ColorFormat::Argb4));
  EXPECT_EQ(parseColor("#00ffffff"), Value::color(0x00ffffff, ColorFormat::Argb8));
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

TEST(ParseFloatValue, NegativeZero) {
  // aapt2 keeps the sign of a float (0x80000000) but not of a dimension or fraction.
  EXPECT_EQ(parseFloatValue("-0"), Value::floating(-0.0f));
  EXPECT_NE(parseFloatValue("-0"), Value::floating(0.0f));
  EXPECT_EQ(parseFloatValue("-0dp"), Value::dimensionFromComplex(0x1));
  EXPECT_EQ(parseFloatValue("-0%"), Value::fractionFromComplex(0x0));
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
  // aapt2 rejects these too ("inf" and "nan" fail its first-character check).
  for (const char* s : {"", "dp", "16 dp", "16DP", "16Dp", "16dpx", "16em", "16px5", ".dp",
                        "-dp", "1e", "1e+dp", "16%%", "1,5dp", "inf", "nan", "1.5f"}) {
    EXPECT_FALSE(parseFloatValue(s)) << '"' << s << '"';
  }
}

TEST(ParseFloatValue, RejectsWhatAaptOnlyTakesThroughStrtof) {
  // aapt2 accepts all of these; see the top of ValueParser.h for why hydroid does not.
  // inf/NaN, hex floats, past FLT_MAX, and dimensions of 2^41 and up (undefined in aapt2).
  for (const char* s : {"-inf", "+nan", "-infinity", "-infdp", "0x10px", "-0x1p4", "0X10",
                        "1e39", "1e39dp", "-1e39dp", "1e39%", "-1e39%", "3.4028236e38", "3e12dp",
                        "2199023255552dp"}) {
    EXPECT_FALSE(parseFloatValue(s)) << '"' << s << '"';
  }
  // Just under 2^41 is still defined.
  EXPECT_TRUE(parseFloatValue("2199023124480dp"));
}

TEST(ParseFloatValue, RoundsToNearestFloatOnce) {
  // A hair above the midpoint of 1 and 1 + 2^-23. Its nearest double is the midpoint itself,
  // so double then float would tie down to 1; aapt2's strtof gives 0x3f800001.
  EXPECT_EQ(parseFloatValue("1.0000000596046447753906251"), Value::floating(1.00000012f));
  EXPECT_EQ(parseFloatValue("1.0000000596046447753906250"), Value::floating(1.0f));  // tie, even
  EXPECT_EQ(parseFloatValue("1.0000000596046447753906249"), Value::floating(1.0f));
  EXPECT_EQ(parseFloatValue("1.000000059604644775390625001"), Value::floating(1.00000012f));  // #6
  // In 8p15 the extra bit is lost, but the radix is not: 1.0 would be 23p0, 0x100.
  EXPECT_EQ(parseFloatValue("1.0000000596046447753906251px"),
            Value::dimensionFromComplex(0x800020));
  // FLT_MAX, the smallest subnormal, and below half of it.
  EXPECT_EQ(parseFloatValue("3.4028235677973366e38"), Value::floating(FLT_MAX));
  EXPECT_EQ(parseFloatValue("1.4e-45"), Value::floating(FLT_TRUE_MIN));
  EXPECT_EQ(parseFloatValue("7e-46"), Value::floating(0));
  EXPECT_EQ(parseFloatValue("1e-400"), Value::floating(0));
}

TEST(ParseFloatValue, AtMost126Characters) {
  // aapt2 copies at most 126 characters into its strtof buffer.
  const std::string zeros(121, '0');
  EXPECT_EQ(parseFloatValue("0." + zeros + "1dp").has_value(), true);       // 126
  EXPECT_EQ(parseFloatValue("0." + zeros + "01dp").has_value(), false);     // 127
  EXPECT_EQ(parseFloatValue("  0." + zeros + "1dp  ").has_value(), true);  // trimmed first
  // The limit is stringToFloat's: an integer has none, so leading zeros only fail as a float.
  const std::string padded = std::string(130, '0') + "42";
  EXPECT_EQ(parseLiteral(padded, kFormatInteger), Value::integer(42));
  EXPECT_EQ(parseLiteral(padded, kFormatAny), Value::integer(42));
  EXPECT_FALSE(parseLiteral(padded, kFormatFloat));
  EXPECT_EQ(parseLiteral("0." + zeros + "001", kFormatAny), Value::floating(0));  // 126
  EXPECT_FALSE(parseLiteral("0." + zeros + "0001", kFormatAny));                 // 127
}

// ---- integer -------------------------------------------------------------------------------

TEST(ParseInteger, Decimal) {
  EXPECT_EQ(parseInteger("0"), Value::integer(0));
  EXPECT_EQ(parseInteger("42"), Value::integer(42));
  EXPECT_EQ(parseInteger("-42"), Value::integer(-42));
  EXPECT_EQ(parseInteger(" 7\n"), Value::integer(7));
  EXPECT_EQ(parseInteger("2147483647"), Value::integer(INT32_MAX));
  EXPECT_EQ(parseInteger("-2147483648"), Value::integer(INT32_MIN));
}

TEST(ParseInteger, Hex) {
  EXPECT_EQ(parseInteger("0x10"), Value::integer(16, IntFormat::Hex));
  EXPECT_EQ(parseInteger("0x7f010000"), Value::integer(0x7f010000, IntFormat::Hex));
  EXPECT_EQ(parseInteger("0xFfFfFfFf"), Value::integer(-1, IntFormat::Hex));
}

TEST(ParseInteger, NegativeZeroIsZero) {
  EXPECT_EQ(parseInteger("-0"), Value::integer(0));
  EXPECT_EQ(parseLiteral("-0", kFormatAny), Value::integer(0));  // integer before float
}

TEST(ParseInteger, HexIsTrimmed) {
  EXPECT_EQ(parseInteger("0x10 "), Value::integer(16, IntFormat::Hex));
  EXPECT_EQ(parseInteger("\t0x10\n"), Value::integer(16, IntFormat::Hex));
}

TEST(ParseInteger, Rejects) {
  for (const char* s : {"", "-", "+5", "2147483648", "-2147483649", "0x", "0X10", "-0x10",
                        "00x10", "0x100000000", "0xg", "4 2", "1.0", "1e3", "x10", "true"}) {
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

TEST(ParseLiteral, NoFormatsTakesNothing) {
  for (const char* s : {"#fff", "true", "5", "0x10", "1.5", "16dp", "50%"}) {
    EXPECT_FALSE(parseLiteral(s, 0)) << '"' << s << '"';
  }
}

TEST(ParseLiteral, DimensionAndFractionNeedTheirOwnFormat) {
  // <fraction> takes 16dp only because it also allows a dimension.
  EXPECT_FALSE(parseLiteral("50%", kFormatDimension));
  EXPECT_FALSE(parseLiteral("50%p", kFormatDimension));
  EXPECT_FALSE(parseLiteral("16dp", kFormatFraction));
  EXPECT_FALSE(parseLiteral("50%", kFormatFloat));
  EXPECT_EQ(parseLiteral("50%", kFormatFraction), Value::fractionFromComplex(0x40000030));
  EXPECT_EQ(parseLiteral("16dp", kFormatDimension), Value::dimensionFromComplex(0x1001));
}

TEST(ParseLiteral, ZeroBeforeHexIsNothing) {
  // Not hex, and not a number with a unit either: aapt2 has no literal for it.
  EXPECT_FALSE(parseLiteral("00x10", kFormatAny));
  EXPECT_FALSE(parseLiteral("00x10", kDimenTag));
}

TEST(ParseLiteral, AaptOrder) {
  // Integer is tried before float, color before everything.
  EXPECT_EQ(parseLiteral("5", kFormatInteger | kFormatFloat), Value::integer(5));
  EXPECT_EQ(parseLiteral("#fff", kFormatAny), Value::color(0xffffffff, ColorFormat::Rgb4));
  EXPECT_EQ(parseLiteral("true", kFormatAny), Value::boolean(true));
  EXPECT_FALSE(parseLiteral("hello", kFormatAny));
}

TEST(ParseLiteral, FloatThatMissesTheDoubleByOneIsAStringWhenOtherFormatsAreAllowed) {
  // aapt2 keeps these as strings under kFormatAny: 9638178525's nearest float is 9638178816.
  EXPECT_FALSE(parseLiteral("9638178525", kFormatAny));
  EXPECT_FALSE(parseLiteral("-2147483649", kFormatAny));
  // A float holds 2^31 exactly, and 0.1 is off by far less than 1.
  EXPECT_EQ(parseLiteral("2147483648", kFormatAny), Value::floating(2147483648.0f));
  EXPECT_EQ(parseLiteral("0.1", kFormatAny), Value::floating(0.1f));
  // Any other format asks for the check, even one with no string to fall back to: aapt2 36.1.0
  // fails a style item of a float|reference, float|integer, float|color, float|boolean, or
  // float enum/flags attr with this text, and keeps it as a string for float|string.
  for (uint32_t other : {kFormatReference, kFormatString, kFormatInteger, kFormatBoolean,
                         kFormatColor, kFormatEnum, kFormatFlags}) {
    EXPECT_FALSE(parseLiteral("9638178525", kFormatFloat | other)) << other;
    EXPECT_FALSE(parseLiteral("-2147483649", kFormatFloat | other)) << other;
  }
  // Floats only: no check, as for <dimen> or a float attr.
  EXPECT_EQ(parseLiteral("9638178525", kFormatFloat), Value::floating(9638178816.0f));
  EXPECT_EQ(parseLiteral("9638178525", kDimenTag), Value::floating(9638178816.0f));
  // Off by exactly 1 fails; less than 1 passes, however large the float.
  EXPECT_FALSE(parseLiteral("16777217.0", kFormatAny));
  EXPECT_EQ(parseLiteral("16777216.5", kFormatAny), Value::floating(16777216.0f));
  EXPECT_EQ(parseLiteral("16777218.0", kFormatAny), Value::floating(16777218.0f));
  EXPECT_EQ(parseLiteral("1e10", kFormatAny), Value::floating(1e10f));
  // Dimensions are never checked.
  EXPECT_EQ(parseLiteral("9638178525px", kFormatAny), parseFloatValue("9638178525px"));
}

// ---- string_view input ----------------------------------------------------------------------

TEST(ParseLiteral, EmbeddedNulIsNotTheEnd) {
  // XML text cannot hold a NUL, but a string_view can; the parsers read all of it rather than
  // stopping at the NUL as a C string would.
  for (std::string_view s : {"16\0dp"sv, "16dp\0"sv, "\0" "16dp"sv, "5\0"sv, "0x10\0"sv,
                             "#fff\0"sv, "true\0"sv, "1.5\0"sv}) {
    EXPECT_FALSE(parseLiteral(s, kFormatAny)) << testing::PrintToString(std::string(s));
  }
}

TEST(ParseLiteral, ReadsNoFurtherThanTheView) {
  EXPECT_EQ(parseLiteral(std::string_view("16dpx", 4), kFormatAny),
            Value::dimensionFromComplex(0x1001));
  EXPECT_EQ(parseLiteral(std::string_view("0x10g", 4), kFormatAny),
            Value::integer(16, IntFormat::Hex));
  EXPECT_EQ(parseLiteral(std::string_view("#fff0", 4), kFormatAny),
            Value::color(0xffffffff, ColorFormat::Rgb4));
  EXPECT_EQ(parseLiteral(std::string_view("truee", 4), kFormatAny), Value::boolean(true));
  EXPECT_EQ(parseLiteral(std::string_view("1.5e", 3), kFormatAny), Value::floating(1.5f));
}

}  // namespace
}  // namespace hydroid::res

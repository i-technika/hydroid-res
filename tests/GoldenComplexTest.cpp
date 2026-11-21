// Golden check for dimensions and fractions: hydroid's complex encoding, getDimension() and
// getFraction() against what Android stored and returned on the emulator (golden/data). That
// hydroid picks the value Android picked under each condition is GoldenMatchTest's.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "GoldenUtil.h"
#include "hydroid/res/Complex.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

using test::forEachGoldenCondition;

constexpr int kTypeDimension = 5;  // TypedValue.TYPE_DIMENSION
constexpr int kTypeFraction = 6;   // TypedValue.TYPE_FRACTION

struct GoldenComplex {
  std::string where;    // "160dpi_en-US_notnight_port_sw320dp dimen/margin"
  std::string key;      // "dimen/margin"
  uint32_t data;        // TypedValue.data
  int type;             // TypedValue.type
  std::string coerced;  // TypedValue.coerceToString(): "16.0dip"
  float result;         // Resources.getDimension(), or getFraction(base, parentBase)
  float base;           // fractions only
  float parentBase;     // fractions only
  uint32_t densityDpi;
  float fontScale;
  DisplayMetrics metrics;  // Resources.getDisplayMetrics() for the condition
};

struct Golden {
  size_t conditions = 0;
  std::vector<GoldenComplex> values;
};

float floatAt(const nlohmann::json& j, const char* key) {
  return static_cast<float>(j.at(key).get<double>());
}

Golden loadGolden() {
  Golden out;
  forEachGoldenCondition([&](const std::string& condition, const nlohmann::json& golden) {
    const auto dpi = golden.at("condition").at("densityDpi").get<uint32_t>();
    const nlohmann::json& m = golden.at("metrics");
    const DisplayMetrics metrics{floatAt(m, "density"), floatAt(m, "scaledDensity"),
                                 floatAt(m, "xdpi")};
    out.conditions++;
    for (const auto& [key, v] : golden.at("values").items()) {
      const bool dimen = key.rfind("dimen/", 0) == 0;
      const bool fraction = key.rfind("fraction/", 0) == 0;
      if (!dimen && !fraction) continue;
      out.values.push_back(
          {condition + " " + key, key, static_cast<uint32_t>(v.at("data").get<int64_t>()),
           v.at("type").get<int>(), v.at("string").get<std::string>(),
           floatAt(v, dimen ? "px" : "fraction"), fraction ? floatAt(v, "base") : 0.0f,
           fraction ? floatAt(v, "parentBase") : 0.0f, dpi, floatAt(m, "fontScale"), metrics});
    }
  });
  return out;
}

const Golden& golden() {
  static const Golden g = loadGolden();
  return g;
}

TEST(GoldenComplex, EveryConditionHasEveryDimensionAndFraction) {
  // 7 densities x 3 languages x 2 x 2 x 3 widths, and the extras of GoldenDumpTest.kt.
  EXPECT_EQ(golden().conditions, 252u + 123u);
  std::set<std::string> keys;
  for (const GoldenComplex& g : golden().values) {
    keys.insert(g.key);
    SCOPED_TRACE(g.where);
    EXPECT_EQ(g.type, g.key.rfind("dimen/", 0) == 0 ? kTypeDimension : kTypeFraction);
  }
  EXPECT_GT(keys.size(), 0u);
  EXPECT_EQ(golden().values.size(), golden().conditions * keys.size());
}

TEST(GoldenComplex, CoercedStringParsesBackToData) {
  for (const GoldenComplex& g : golden().values) {
    SCOPED_TRACE(g.where + " \"" + g.coerced + "\"");
    auto v = parseFloatValue(g.coerced);
    ASSERT_TRUE(v);
    EXPECT_EQ(v->kind(), g.type == kTypeDimension ? ValueKind::Dimension : ValueKind::Fraction);
    EXPECT_EQ(v->complexData(), g.data);
  }
}

TEST(GoldenComplex, ComplexToDimensionMatchesGetDimension) {
  for (const GoldenComplex& g : golden().values) {
    if (g.type != kTypeDimension) continue;
    SCOPED_TRACE(g.where);
    EXPECT_EQ(complexToDimension(g.data, g.metrics), g.result);
  }
}

TEST(GoldenComplex, ComplexToFractionMatchesGetFraction) {
  for (const GoldenComplex& g : golden().values) {
    if (g.type != kTypeFraction) continue;
    SCOPED_TRACE(g.where);
    EXPECT_EQ(complexToFraction(g.data, g.base, g.parentBase), g.result);
  }
}

TEST(GoldenComplex, ForDensityMatchesDisplayMetrics) {
  // xdpi is left out: Android takes it from the display (420 on the golden emulator).
  for (const GoldenComplex& g : golden().values) {
    SCOPED_TRACE(g.where);
    const DisplayMetrics m = DisplayMetrics::forDensity(g.densityDpi, g.fontScale);
    EXPECT_EQ(m.density, g.metrics.density);
    EXPECT_EQ(m.scaledDensity, g.metrics.scaledDensity);
  }
}

}  // namespace
}  // namespace hydroid::res

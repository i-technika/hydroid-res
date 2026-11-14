// Golden check for dimensions: hydroid's complex encoding and getDimension() against what
// Android stored and returned on the emulator (golden/data).

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hydroid/res/Complex.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

namespace fs = std::filesystem;

constexpr int kTypeDimension = 5;  // TypedValue.TYPE_DIMENSION

struct GoldenDimen {
  std::string where;    // "160dpi_en-US_notnight_port_sw320dp dimen/margin"
  std::string name;     // "margin"
  uint32_t data;        // TypedValue.data
  int type;             // TypedValue.type
  std::string coerced;  // TypedValue.coerceToString(): "16.0dip"
  float px;             // Resources.getDimension()
  uint32_t densityDpi;
};

std::vector<GoldenDimen> loadGoldenDimens() {
  std::vector<GoldenDimen> out;
  for (const auto& file : fs::directory_iterator(HYDROID_GOLDEN_DIR)) {
    if (file.path().extension() != ".json") continue;
    std::ifstream in(file.path());
    const nlohmann::json golden = nlohmann::json::parse(in);
    const auto dpi = golden["condition"]["densityDpi"].get<uint32_t>();
    for (const auto& [key, v] : golden["values"].items()) {
      if (key.rfind("dimen/", 0) != 0) continue;
      out.push_back({file.path().stem().string() + " " + key, key.substr(6),
                     static_cast<uint32_t>(v["data"].get<int64_t>()), v["type"].get<int>(),
                     v["string"].get<std::string>(), static_cast<float>(v["px"].get<double>()),
                     dpi});
    }
  }
  return out;
}

const std::vector<GoldenDimen>& goldenDimens() {
  static const std::vector<GoldenDimen> dimens = loadGoldenDimens();
  return dimens;
}

// Every <dimen> text in the golden app's values*/ folders, parsed by hydroid, by name.
std::map<std::string, std::set<uint32_t>> parsedSourceDimens() {
  std::map<std::string, std::set<uint32_t>> out;
  for (const auto& dir : fs::directory_iterator(HYDROID_TEST_RES_DIR)) {
    if (dir.path().filename().string().rfind("values", 0) != 0) continue;
    for (const auto& file : fs::directory_iterator(dir.path())) {
      pugi::xml_document doc;
      if (file.path().extension() != ".xml" || !doc.load_file(file.path().c_str())) continue;
      for (pugi::xml_node dimen : doc.child("resources").children("dimen")) {
        auto v = parseLiteral(dimen.child_value(), kFormatDimension);
        if (v) out[dimen.attribute("name").value()].insert(v->complex);
      }
    }
  }
  return out;
}

TEST(GoldenDimen, DataIsPresent) {
  // 252 conditions × 3 dimens.
  EXPECT_EQ(goldenDimens().size(), 252u * 3);
}

TEST(GoldenDimen, SourceTextEncodesToAndroidData) {
  const auto sources = parsedSourceDimens();
  for (const GoldenDimen& g : goldenDimens()) {
    SCOPED_TRACE(g.where);
    EXPECT_EQ(g.type, kTypeDimension);
    // Config matching comes later, so check that one of the resource's sources gives this data.
    auto it = sources.find(g.name);
    ASSERT_NE(it, sources.end());
    EXPECT_EQ(it->second.count(g.data), 1u) << "data 0x" << std::hex << g.data;
  }
}

TEST(GoldenDimen, CoercedStringParsesBackToData) {
  for (const GoldenDimen& g : goldenDimens()) {
    SCOPED_TRACE(g.where + " \"" + g.coerced + "\"");
    auto v = parseFloatValue(g.coerced);
    ASSERT_TRUE(v);
    EXPECT_EQ(v->kind, ValueKind::Dimension);
    EXPECT_EQ(v->complex, g.data);
  }
}

TEST(GoldenDimen, ComplexToDimensionMatchesGetDimension) {
  for (const GoldenDimen& g : goldenDimens()) {
    SCOPED_TRACE(g.where);
    EXPECT_EQ(complexToDimension(g.data, DisplayMetrics::forDensity(g.densityDpi)), g.px);
  }
}

}  // namespace
}  // namespace hydroid::res

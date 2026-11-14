// Golden check for colors: hydroid's parsed <color> text against the Res_value type and data
// Android returned on the emulator (golden/data). The type is which of #RGB ... #AARRGGBB was used.

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

namespace fs = std::filesystem;

struct GoldenColor {
  std::string where;  // "160dpi_en-US_notnight_port_sw320dp color/accent"
  std::string key;    // "color/accent"
  int type;           // TypedValue.type
  uint32_t data;      // TypedValue.data
};

std::vector<GoldenColor> loadGolden() {
  std::vector<GoldenColor> out;
  for (const auto& file : fs::directory_iterator(HYDROID_GOLDEN_DIR)) {
    if (file.path().extension() != ".json") continue;
    std::ifstream in(file.path());
    const nlohmann::json golden = nlohmann::json::parse(in);
    for (const auto& [key, v] : golden.at("values").items()) {
      if (key.rfind("color/", 0) != 0) continue;
      out.push_back({file.path().stem().string() + " " + key, key, v.at("type").get<int>(),
                     static_cast<uint32_t>(v.at("data").get<int64_t>())});
    }
  }
  return out;
}

const std::vector<GoldenColor>& golden() {
  static const std::vector<GoldenColor> g = loadGolden();
  return g;
}

using TypeAndData = std::pair<int, uint32_t>;

// Every <color> in the golden app's values*/ folders as (type, data), by "color/name". A
// "@color/x" source takes x's values; references are resolved properly in a later step.
std::map<std::string, std::set<TypeAndData>> parsedSources() {
  std::map<std::string, std::set<TypeAndData>> out;
  std::map<std::string, std::string> aliases;
  for (const auto& dir : fs::directory_iterator(HYDROID_TEST_RES_DIR)) {
    if (dir.path().filename().string().rfind("values", 0) != 0) continue;
    for (const auto& file : fs::directory_iterator(dir.path())) {
      pugi::xml_document doc;
      if (file.path().extension() != ".xml" || !doc.load_file(file.path().c_str())) continue;
      for (pugi::xml_node node : doc.child("resources").children("color")) {
        const std::string key = std::string("color/") + node.attribute("name").value();
        const std::string text = node.child_value();
        if (text.rfind("@color/", 0) == 0) {
          aliases[key] = text.substr(1);
          continue;
        }
        auto v = parseLiteral(text, kFormatColor);
        if (!v || v->kind != ValueKind::Color) {
          ADD_FAILURE() << file.path().string() << ": " << key << " \"" << text
                        << "\" does not parse to a color";
          continue;
        }
        out[key].insert({static_cast<int>(v->color.format), v->color.argb});
      }
    }
  }
  for (const auto& [key, target] : aliases) {
    const auto it = out.find(target);
    if (it != out.end()) out[key].insert(it->second.begin(), it->second.end());
  }
  return out;
}

TEST(GoldenColor, SourcesCoverGolden) {
  const auto sources = parsedSources();
  EXPECT_GT(golden().size(), 0u);
  for (const GoldenColor& g : golden()) {
    EXPECT_EQ(sources.count(g.key), 1u) << g.where;
  }
}

TEST(GoldenColor, SourceTextGivesAndroidTypeAndData) {
  const auto sources = parsedSources();
  for (const GoldenColor& g : golden()) {
    SCOPED_TRACE(g.where);
    // Config matching comes later, so check that one of the resource's sources gives this value.
    auto it = sources.find(g.key);
    ASSERT_NE(it, sources.end());
    EXPECT_EQ(it->second.count({g.type, g.data}), 1u)
        << "type " << g.type << ", data 0x" << std::hex << g.data;
  }
}

}  // namespace
}  // namespace hydroid::res

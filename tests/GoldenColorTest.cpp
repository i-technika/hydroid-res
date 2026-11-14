// Golden check for colors: hydroid's parsed <color> text against the Res_value type and data
// Android returned on the emulator (golden/data). The type is which of #RGB ... #AARRGGBB was used.

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "GoldenUtil.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

namespace fs = std::filesystem;
using test::forEachGoldenCondition;
using test::forEachValuesNode;
using test::trimmedText;

struct GoldenColor {
  std::string where;  // "160dpi_en-US_notnight_port_sw320dp color/accent"
  std::string key;    // "color/accent"
  int type;           // TypedValue.type
  uint32_t data;      // TypedValue.data
};

struct Golden {
  size_t conditions = 0;
  std::vector<GoldenColor> values;
};

Golden loadGolden() {
  Golden out;
  forEachGoldenCondition([&](const std::string& condition, const nlohmann::json& golden) {
    out.conditions++;
    for (const auto& [key, v] : golden.at("values").items()) {
      if (key.rfind("color/", 0) != 0) continue;
      out.values.push_back({condition + " " + key, key, v.at("type").get<int>(),
                            static_cast<uint32_t>(v.at("data").get<int64_t>())});
    }
  });
  return out;
}

const Golden& golden() {
  static const Golden g = loadGolden();
  return g;
}

using TypeAndData = std::pair<int, uint32_t>;
using Sources = std::map<std::string, std::set<TypeAndData>>;

// Every <color> in the golden app's values*/ folders as (type, data), by "color/name". A
// "@color/x" source takes all of x's values, through aliases of aliases; references are resolved
// properly in a later step.
Sources loadSources() {
  Sources out;
  std::map<std::string, std::set<std::string>> aliases;
  forEachValuesNode([&](const fs::path& file, pugi::xml_node node) {
    if (std::string_view(node.name()) != "color") return;
    const std::string key = std::string("color/") + node.attribute("name").value();
    const std::string text = trimmedText(node);
    if (text.rfind("@color/", 0) == 0) {
      aliases[key].insert(text.substr(1));
      return;
    }
    if (!text.empty() && (text[0] == '@' || text[0] == '?')) {
      ADD_FAILURE() << file.string() << ": " << key << " \"" << text
                    << "\" is a reference this test cannot resolve yet";
      return;
    }
    auto v = parseLiteral(text, kFormatColor);
    if (!v || v->kind != ValueKind::Color) {
      ADD_FAILURE() << file.string() << ": " << key << " \"" << text
                    << "\" does not parse to a color";
      return;
    }
    out[key].insert({static_cast<int>(v->color.format), v->color.argb});
  });
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto& [key, targets] : aliases) {
      for (const std::string& target : targets) {
        const auto it = out.find(target);
        if (it == out.end()) continue;
        for (const TypeAndData& v : it->second) {
          if (out[key].insert(v).second) changed = true;
        }
      }
    }
  }
  return out;
}

const Sources& sources() {
  static const Sources s = loadSources();
  return s;
}

TEST(GoldenColor, EveryConditionHasEverySource) {
  EXPECT_EQ(golden().conditions, 252u);  // 7 densities × 3 languages × 2 × 2 × 3 widths
  EXPECT_GT(sources().size(), 0u);
  EXPECT_EQ(golden().values.size(), golden().conditions * sources().size());
}

TEST(GoldenColor, SourceTextGivesAndroidTypeAndData) {
  for (const GoldenColor& g : golden().values) {
    SCOPED_TRACE(g.where);
    // Config matching comes later, so check that one of the resource's sources gives this value.
    auto it = sources().find(g.key);
    ASSERT_NE(it, sources().end());
    EXPECT_EQ(it->second.count({g.type, g.data}), 1u)
        << "type " << g.type << ", data 0x" << std::hex << g.data;
  }
}

}  // namespace
}  // namespace hydroid::res

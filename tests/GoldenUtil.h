// Walking golden/data and the golden app's values*/ resources, shared by the golden tests.

#pragma once

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace hydroid::res::test {

// Calls fn(condition, json) for each golden/data/*.json. The condition is the file stem,
// "160dpi_en-US_notnight_port_sw320dp".
template <typename Fn>
void forEachGoldenCondition(Fn&& fn) {
  for (const auto& file : std::filesystem::directory_iterator(HYDROID_GOLDEN_DIR)) {
    if (file.path().extension() != ".json") continue;
    std::ifstream in(file.path());
    fn(file.path().stem().string(), nlohmann::json::parse(in));
  }
}

// Calls fn(file, node) for each element under <resources> in the golden app's values*/ XML.
template <typename Fn>
void forEachValuesNode(Fn&& fn) {
  for (const auto& dir : std::filesystem::directory_iterator(HYDROID_TEST_RES_DIR)) {
    if (dir.path().filename().string().rfind("values", 0) != 0) continue;
    for (const auto& file : std::filesystem::directory_iterator(dir.path())) {
      pugi::xml_document doc;
      if (file.path().extension() != ".xml" || !doc.load_file(file.path().c_str())) continue;
      for (pugi::xml_node node : doc.child("resources").children()) fn(file.path(), node);
    }
  }
}

// The node's text without the surrounding whitespace, which aapt2 drops.
inline std::string trimmedText(pugi::xml_node node) {
  std::string_view s = node.child_value();
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(first, last - first + 1));
}

}  // namespace hydroid::res::test

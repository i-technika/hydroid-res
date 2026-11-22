// Replaying the lifecycles a golden app other than golden/app dumped (golden/localeconfig/data,
// golden/nolocale/data): for each, the steps a Resources went through on Android, each the value
// hydroid picks of every string against the one Android picked, and the locale the configuration
// has first. Each string of those apps' res/ says the folder it is in. The table has the system's
// locales too, as the app's AssetManager on the device has them.

#pragma once

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "GoldenUtil.h"
#include "TempDir.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"

namespace hydroid::res::test {

// The locales of the system's resources on the golden emulator, as folder qualifiers: "ja", "iw",
// "zh-rHK", "b+sr+Latn". NoLocaleDumpTest dumps them, of AssetManager.getLocales, as
// golden/nolocale/data/system_locales.txt. That gives Tagalog as Filipino
// (ResTable_config::getBcp47Locale, canonicalizing), of the folder values-tl/ the framework has
// (golden/aosp/framework_res_dirs.txt), so the folder is tl here, as on the device.
inline std::set<std::string> frameworkLocales() {
  const std::filesystem::path file =
      std::filesystem::path(HYDROID_GOLDEN_NOLOCALE_DIR) / "system_locales.txt";
  std::ifstream in(file);
  EXPECT_TRUE(in) << "cannot open " << file;
  std::set<std::string> locales;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::optional<Config> locale = parseLanguageTag(line);
    EXPECT_TRUE(locale) << line;
    if (!locale) continue;
    if (std::string(locale->lang.data()) == "fil") locale->lang = {'t', 'l'};
    locales.insert(toString(*locale));
  }
  return locales;
}

// Adds to `table` a package android with a folder of `dir` for each of frameworkLocales(), which
// is what of the system's resources Resources picks a locale by, and the attributes the golden
// apps' XML files name of it, which are linked once a package android is loaded.
inline void addFrameworkPackage(ResourceTable& table, const TempDir& dir) {
  dir.write("android/values/attrs.xml",
            "<resources><attr name=\"name\" format=\"string\"/>"
            "<attr name=\"defaultLocale\" format=\"string\"/>"
            "<public type=\"attr\" name=\"name\"/><public type=\"attr\" name=\"defaultLocale\"/>"
            "</resources>");
  for (const std::string& locale : frameworkLocales()) {
    dir.write("android/values-" + locale + "/strings.xml",
              "<resources><string name=\"where\">" + locale + "</string></resources>");
  }
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(table.addPackage(dir.path() / "android", "android", kFrameworkPackageId, &errors))
      << (errors.empty() ? std::string() : errors[0].message);
}

inline std::string pickedText(const ResourceTable& table, const Resources& resources,
                              const std::string& package, const std::string& name) {
  const std::optional<ResId> id = table.find(package, ResType::String, name);
  if (!id) return "no resource";
  const Candidate* c = resources.candidate(*id);
  if (c == nullptr) return "none";
  return table.package(package)->strings.at(c->value.index());
}

// The locale of `c` alone, under the code a LocaleList gives it.
inline std::string firstLocale(const Config& c) {
  Config locale;
  locale.lang = c.lang;
  if (std::string(c.lang.data()) == "iw") locale.lang = {'h', 'e'};
  locale.region = c.region;
  locale.script = c.script;
  locale.variant = c.variant;
  return toString(locale);
}

// Replays every lifecycle of `dir` on a Resources of `table`, and checks the strings of
// `package`. `localeConfig` is the app's, which ContextImpl.setResources gives the Resources it has
// made if it has more than one locale then: after the first step has chosen without it.
inline void expectLifecyclesMatch(const char* dir, const ResourceTable& table,
                                  const std::string& package, const LocaleConfig* localeConfig) {
  size_t lifecycles = 0;
  for (const auto& file : std::filesystem::directory_iterator(dir)) {
    if (file.path().extension() != ".json") continue;
    lifecycles++;
    std::ifstream in(file.path());
    const nlohmann::json golden = nlohmann::json::parse(in);
    SCOPED_TRACE(golden.at("name").get<std::string>());
    Resources resources(table);
    bool first = true;
    for (const nlohmann::json& step : golden.at("steps")) {
      SCOPED_TRACE(step.at("step").get<std::string>());
      const GoldenDevice device = goldenDevice(step.at("condition"));
      resources.setConfiguration(device.config, device.locales);
      if (first && golden.at("localeConfig").get<bool>()) {
        ASSERT_NE(localeConfig, nullptr) << "the app has no LocaleConfig";
        resources.setLocaleConfig(*localeConfig);
      }
      first = false;

      const std::string locales =
          step.at("condition").at("configuration").at("locales").get<std::string>();
      const std::optional<Config> chosen = parseLanguageTag(locales.substr(0, locales.find(',')));
      ASSERT_TRUE(chosen) << locales;
      EXPECT_EQ(firstLocale(resources.configuration()), firstLocale(*chosen)) << locales;
      // Every string of the res/ hydroid reads is one the app has: aapt2 link drops one that has
      // no value without qualifiers, which hydroid keeps (ResourceTable.h).
      size_t strings = 0;
      for (const TypeTable& type : table.package(package)->types) {
        if (type.type != ResType::String) continue;
        for (const Entry& entry : type.entries) {
          strings++;
          EXPECT_TRUE(step.at("values").contains("string/" + entry.name)) << entry.name;
        }
      }
      EXPECT_EQ(strings, step.at("values").size());
      for (const auto& [key, v] : step.at("values").items()) {
        const std::string name = key.substr(key.find('/') + 1);
        const std::string want =
            v.at("string").is_null() ? "none" : v.at("string").get<std::string>();
        EXPECT_EQ(pickedText(table, resources, package, name), want) << key << " of " << locales;
      }
    }
  }
  EXPECT_GT(lifecycles, 0u) << "no golden JSON in " << dir;
}

}  // namespace hydroid::res::test

// Golden check for an app with a LocaleConfig and a default locale (#249): for each lifecycle of
// golden/localeconfig/data, the steps a Resources goes through on Android, each the value hydroid
// picks of every string against the one Android picked, and the locale the configuration has
// first. Each string of golden/localeconfig's res/ says the folder it is in.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "GoldenUtil.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"

namespace hydroid::res {
namespace {

using test::goldenDevice;

constexpr const char* kPackage = "com.itechnika.hydroid.golden.localeconfig";

const ResourceTable& table() {
  static const ResourceTable t = [] {
    ResourceTable loaded;
    std::vector<Diagnostic> errors;
    EXPECT_TRUE(loaded.addPackage(HYDROID_LOCALECONFIG_RES_DIR, kPackage, kAppPackageId, &errors))
        << (errors.empty() ? std::string() : errors[0].message);
    return loaded;
  }();
  return t;
}

const LocaleConfig& localeConfig() {
  static const LocaleConfig config = [] {
    std::vector<Diagnostic> errors;
    std::optional<LocaleConfig> parsed = parseLocaleConfig(
        std::filesystem::path(HYDROID_LOCALECONFIG_RES_DIR) / "xml" / "locales_config.xml",
        &errors);
    EXPECT_TRUE(parsed) << (errors.empty() ? std::string() : errors[0].message);
    return parsed.value_or(LocaleConfig{});
  }();
  return config;
}

std::string pickedText(const Resources& resources, const std::string& name) {
  const std::optional<ResId> id = table().find(kPackage, ResType::String, name);
  if (!id) return "no resource";
  const Candidate* c = resources.candidate(*id);
  if (c == nullptr) return "none";
  return table().package(kPackage)->strings.at(c->value.index());
}

std::string firstLocale(const Config& c) {
  Config locale;
  locale.lang = c.lang;
  if (std::string(c.lang.data()) == "iw") locale.lang = {'h', 'e'};
  locale.region = c.region;
  locale.script = c.script;
  locale.variant = c.variant;
  return toString(locale);
}

TEST(GoldenLocaleConfig, PicksWhatAndroidPicked) {
  size_t lifecycles = 0;
  for (const auto& file : std::filesystem::directory_iterator(HYDROID_GOLDEN_LOCALECONFIG_DIR)) {
    if (file.path().extension() != ".json") continue;
    lifecycles++;
    std::ifstream in(file.path());
    const nlohmann::json golden = nlohmann::json::parse(in);
    SCOPED_TRACE(golden.at("name").get<std::string>());
    Resources resources(table());
    bool first = true;
    for (const nlohmann::json& step : golden.at("steps")) {
      SCOPED_TRACE(step.at("step").get<std::string>());
      const test::GoldenDevice device = goldenDevice(step.at("condition"));
      resources.setConfiguration(device.config, device.locales);
      // ContextImpl.setResources gives the Resources it has made its app's LocaleConfig, if it has
      // more than one locale then: after the first step has chosen without it.
      if (first && golden.at("localeConfig").get<bool>()) resources.setLocaleConfig(localeConfig());
      first = false;

      const std::string locales =
          step.at("condition").at("configuration").at("locales").get<std::string>();
      const std::optional<Config> chosen = parseLanguageTag(locales.substr(0, locales.find(',')));
      ASSERT_TRUE(chosen) << locales;
      EXPECT_EQ(firstLocale(resources.configuration()), firstLocale(*chosen)) << locales;
      // Every string of the res/ hydroid reads is one the app has: aapt2 link drops one that has
      // no value without qualifiers, which hydroid keeps (ResourceTable.h).
      size_t strings = 0;
      for (const TypeTable& type : table().package(kPackage)->types) {
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
        EXPECT_EQ(pickedText(resources, name), want) << key << " of " << locales;
      }
    }
  }
  EXPECT_GT(lifecycles, 0u);
}

}  // namespace
}  // namespace hydroid::res

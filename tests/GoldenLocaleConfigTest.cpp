// Golden check for an app with a LocaleConfig and a default locale (#249): the lifecycles of
// golden/localeconfig/data, as GoldenLifecycle.h replays them. Its first steps choose without the
// LocaleConfig, and two of them check that English is taken to be there (#252), and of the app's
// locales only, not the system's.

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "GoldenLifecycle.h"
#include "TempDir.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"

namespace hydroid::res {
namespace {

constexpr const char* kPackage = "com.itechnika.hydroid.golden.localeconfig";

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

TEST(GoldenLocaleConfig, PicksWhatAndroidPicked) {
  test::TempDir dir;
  ResourceTable table;
  ASSERT_NO_FATAL_FAILURE(test::addFrameworkPackage(table, dir));
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(table.addPackage(HYDROID_LOCALECONFIG_RES_DIR, kPackage, kAppPackageId, &errors))
      << (errors.empty() ? std::string() : errors[0].message);
  test::expectLifecyclesMatch(HYDROID_GOLDEN_LOCALECONFIG_DIR, table, kPackage, &localeConfig());
}

}  // namespace
}  // namespace hydroid::res

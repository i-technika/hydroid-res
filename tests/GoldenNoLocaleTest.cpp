// Golden check for an app with no locales but the pseudo-locales (#252): the lifecycles of
// golden/nolocale/data, as GoldenLifecycle.h replays them. Android picks for it of the locales
// the system's resources have, which the table has as GoldenLifecycle.h's package android.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "GoldenLifecycle.h"
#include "TempDir.h"
#include "hydroid/res/ResourceTable.h"

namespace hydroid::res {
namespace {

constexpr const char* kPackage = "com.itechnika.hydroid.golden.nolocale";

TEST(GoldenNoLocale, PicksWhatAndroidPicked) {
  // The lifecycles are of languages the system has and has not (NoLocaleDumpTest).
  const std::set<std::string> system = test::frameworkLocales();
  for (const char* has : {"ja", "ko", "iw", "tl", "en-rGB"}) EXPECT_TRUE(system.count(has)) << has;
  for (const char* hasNot : {"cy", "ga", "en-rXA", "ar-rXB"}) {
    EXPECT_FALSE(system.count(hasNot)) << hasNot;
  }

  test::TempDir dir;
  ResourceTable table;
  ASSERT_NO_FATAL_FAILURE(test::addFrameworkPackage(table, dir));
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(table.addPackage(HYDROID_NOLOCALE_RES_DIR, kPackage, kAppPackageId, &errors))
      << (errors.empty() ? std::string() : errors[0].message);
  test::expectLifecyclesMatch(HYDROID_GOLDEN_NOLOCALE_DIR, table, kPackage, nullptr);
}

}  // namespace
}  // namespace hydroid::res

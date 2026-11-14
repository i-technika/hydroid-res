// Step 1 completion check: the toolchain, pugixml and GoogleTest work on every OS.

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <cstring>
#include <filesystem>

#include "hydroid/res/ResId.h"
#include "hydroid/res/Version.h"
#include "hydroid_res.h"

namespace hydroid::res {
namespace {

TEST(Smoke, VersionMatchesCApi) {
  EXPECT_STREQ(version(), hydroid_res_version());
}

TEST(Smoke, ResIdLayout) {
  constexpr ResId id = makeResId(kAppPackageId, 0x02, 0x0013);
  static_assert(id == 0x7f020013);
  EXPECT_EQ(packageId(id), 0x7f);
  EXPECT_EQ(typeId(id), 0x02);
  EXPECT_EQ(entryId(id), 0x0013);
}

TEST(Smoke, PugixmlParsesValues) {
  const char* xml =
      "<resources><string name=\"app_name\">Hydroid</string>"
      "<color name=\"accent\">#FF6200EE</color></resources>";
  pugi::xml_document doc;
  ASSERT_TRUE(doc.load_string(xml));
  auto str = doc.child("resources").find_child_by_attribute("string", "name", "app_name");
  EXPECT_STREQ(str.child_value(), "Hydroid");
}

TEST(Smoke, TestAppResourcesPresent) {
  EXPECT_TRUE(std::filesystem::is_directory(HYDROID_TEST_RES_DIR "/values"));
}

}  // namespace
}  // namespace hydroid::res

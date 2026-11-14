// Step 1 completion check: the toolchain, pugixml and GoogleTest work on every OS.

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <filesystem>
#include <string>

#include "hydroid/res/ResId.h"
#include "hydroid/res/Version.h"
#include "hydroid_res.h"

namespace hydroid::res {
namespace {

// The headers, and the library built from them, carry the version project() gives (#13).
TEST(Smoke, VersionMatchesProject) {
  EXPECT_STREQ(HYDROID_RES_VERSION, HYDROID_PROJECT_VERSION);
  EXPECT_EQ(std::to_string(HYDROID_RES_VERSION_MAJOR) + "." +
                std::to_string(HYDROID_RES_VERSION_MINOR) + "." +
                std::to_string(HYDROID_RES_VERSION_PATCH),
            HYDROID_RES_VERSION);
}

// In one build the library, through either API, is the version of its headers. (A consumer of
// a shared library needs only a compatible one: README.md says when it is.)
TEST(Smoke, VersionMatchesCApi) {
  EXPECT_STREQ(version(), HYDROID_RES_VERSION);
  EXPECT_STREQ(hydroid_res_version(), HYDROID_RES_VERSION);
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

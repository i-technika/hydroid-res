// Every reference hydroid is checked against comes from the API level of kEmulatedSdkVersion
// (#14): the golden dump's emulator, the golden app's compileSdk and targetSdk, the build-tools
// and platform the aapt2 golden files were made with, and the release the AOSP framework res/
// folder list comes from. Raising one without the rest fails here.

#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "GoldenUtil.h"
#include "hydroid/res/Version.h"

namespace hydroid::res {
namespace {

using test::forEachGoldenCondition;

const std::string kLevel = std::to_string(kEmulatedSdkVersion);

TEST(SdkBaseline, GoldenDumpRanOnEmulatedLevel) {
  int conditions = 0;
  forEachGoldenCondition([&](const std::string& condition, const nlohmann::json& golden) {
    ++conditions;
    const nlohmann::json& c = golden.at("condition");
    EXPECT_EQ(c.at("sdkInt").get<int>(), kEmulatedSdkVersion) << condition;
    // Build.VERSION.SDK_INT_FULL is major * 100000 + minor: 37.0, not 37.1 or 37.2.
    ASSERT_TRUE(c.contains("sdkIntFull")) << condition;
    EXPECT_EQ(c.at("sdkIntFull").get<int>(), kEmulatedSdkVersion * 100000) << condition;
  });
  EXPECT_GT(conditions, 0) << "no golden JSON in " << HYDROID_GOLDEN_DIR;
}

TEST(SdkBaseline, GoldenAppBuildsForEmulatedLevel) {
  std::ifstream in(HYDROID_GOLDEN_APP_GRADLE);
  ASSERT_TRUE(in) << "cannot open " << HYDROID_GOLDEN_APP_GRADLE;
  std::stringstream gradle;
  gradle << in.rdbuf();
  for (const char* key : {"compileSdk", "targetSdk"}) {
    std::smatch m;
    const std::string text = gradle.str();
    ASSERT_TRUE(std::regex_search(text, m, std::regex(std::string(key) + R"(\s*=\s*(\d+))")))
        << key << " not found in " << HYDROID_GOLDEN_APP_GRADLE;
    EXPECT_EQ(m[1].str(), kLevel) << key;
  }
}

// The first line of each file names them: "... with build-tools 37.0.0 and android-37.0 on
// Windows. ...". The platform must be <level>.0, not a minor release; any build-tools <level>.x
// will do. The OS is GOLDEN_HOST in gen_aapt2_values.py (#62): aapt2 reads a number past
// 2147483647 in sw<N>dp, w<N>dp, h<N>dp or <N>dpi through the host's long, so another OS would
// change rows of configs.tsv without failing Aapt2ConfigTest.
TEST(SdkBaseline, Aapt2GoldenUsesEmulatedLevel) {
  const std::regex tools(R"(build-tools (\d+)\.\S+ and (android-\d+(\.\d+)?) on (\w+)\.)");
  for (const char* path : {HYDROID_AAPT2_VALUES, HYDROID_AAPT2_CONFIGS}) {
    std::ifstream in(path);
    ASSERT_TRUE(in) << "cannot open " << path;
    std::string header;
    std::getline(in, header);
    std::smatch m;
    ASSERT_TRUE(std::regex_search(header, m, tools)) << path << ": " << header;
    EXPECT_EQ(m[1].str(), kLevel) << path << ": build-tools";
    EXPECT_EQ(m[2].str(), "android-" + kLevel + ".0") << path << ": platform";
    EXPECT_EQ(m[4].str(), "Windows") << path << ": OS";
  }
}

// "... in AOSP, android17-release at <commit> ...": API 37 is Android 17.
TEST(SdkBaseline, AospFolderListIsOfEmulatedLevel) {
  std::ifstream in(HYDROID_AOSP_RES_DIRS);
  ASSERT_TRUE(in) << "cannot open " << HYDROID_AOSP_RES_DIRS;
  std::string header;
  std::getline(in, header);
  std::smatch m;
  ASSERT_TRUE(std::regex_search(header, m, std::regex(R"(android(\d+)-release at)"))) << header;
  EXPECT_EQ(std::stoi(m[1].str()) + 20, kEmulatedSdkVersion) << header;
}

}  // namespace
}  // namespace hydroid::res

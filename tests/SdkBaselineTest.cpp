// Every reference hydroid is checked against comes from the API level of kEmulatedSdkVersion
// (#14): the golden dump's emulator, the golden app's compileSdk and targetSdk, and the
// build-tools and platform the aapt2 golden files were made with. Raising one without the rest
// fails here.

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
    EXPECT_EQ(golden.at("condition").at("sdkInt").get<int>(), kEmulatedSdkVersion) << condition;
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

// The first line of each file names them: "... with build-tools 36.1.0 and android-36. ...".
TEST(SdkBaseline, Aapt2GoldenUsesEmulatedLevel) {
  for (const char* path : {HYDROID_AAPT2_VALUES, HYDROID_AAPT2_CONFIGS}) {
    std::ifstream in(path);
    ASSERT_TRUE(in) << "cannot open " << path;
    std::string header;
    std::getline(in, header);
    std::smatch m;
    ASSERT_TRUE(
        std::regex_search(header, m, std::regex(R"(build-tools (\d+)\.\S+ and android-(\d+)\.)")))
        << path << ": " << header;
    EXPECT_EQ(m[1].str(), kLevel) << path << ": build-tools";
    EXPECT_EQ(m[2].str(), kLevel) << path << ": platform";
  }
}

}  // namespace
}  // namespace hydroid::res

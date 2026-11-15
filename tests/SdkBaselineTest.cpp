// Every reference hydroid is checked against comes from the API level of kEmulatedSdkVersion
// (#14): the golden dump's emulator, the golden app's compileSdk and targetSdk, the build-tools
// and platform the aapt2 golden files were made with, and the release the AOSP framework res/
// folder list comes from. Raising one without the rest fails here.

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

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

// The value gen_aapt2_values.py gives `name`, which it must set once, on a line of its own:
// `NAME = "..."`. Python keeps the last of two, so any other line that starts with the name and
// then =, :, +=, a comma or the like fails too, even below the first. One inside a block, or with
// the name second in `A, NAME = ...`, is not seen. Read a line at a time, as
// std::regex::multiline is not in every standard library.
void readAapt2ScriptPin(const std::string& name, std::string* value) {
  std::ifstream in(HYDROID_AAPT2_SCRIPT);
  ASSERT_TRUE(in) << "cannot open " << HYDROID_AAPT2_SCRIPT;
  const std::regex assigns(name + R"(\s*[^\w\s.].*)");
  const std::regex pin(name + R"re(\s*=\s*"([^"]*)"\s*)re");
  int assignments = 0;
  for (std::string line; std::getline(in, line);) {
    if (!std::regex_match(line, assigns)) continue;
    ++assignments;
    std::smatch m;
    ASSERT_TRUE(std::regex_match(line, m, pin))
        << name << " must be set as " << name << " = \"...\" alone on its line, not: " << line;
    *value = m[1].str();
  }
  ASSERT_NE(assignments, 0) << name << " not found in " << HYDROID_AAPT2_SCRIPT;
  ASSERT_EQ(assignments, 1) << name << " is set " << assignments << " times in "
                            << HYDROID_AAPT2_SCRIPT;
}

// gen_aapt2_values.py pins the build-tools, platform and OS of the aapt2 golden files (#64). The
// platform must be <level>.0, not a minor release; any build-tools <level>.x will do.
TEST(SdkBaseline, Aapt2ScriptPinsEmulatedLevel) {
  std::string buildTools;
  std::string platform;
  ASSERT_NO_FATAL_FAILURE(readAapt2ScriptPin("BUILD_TOOLS", &buildTools));
  ASSERT_NO_FATAL_FAILURE(readAapt2ScriptPin("PLATFORM", &platform));
  EXPECT_TRUE(buildTools.rfind(kLevel + ".", 0) == 0)
      << "BUILD_TOOLS " << buildTools << " is not of level " << kLevel;
  EXPECT_EQ(platform, "android-" + kLevel + ".0") << "PLATFORM";
}

// The first line of each file names what made it: "... with build-tools 37.0.0 and android-37.0
// on Windows (AMD64). ...". All three must be what the script pins, so that changing a pin without
// making the files again fails too (#64). The OS is GOLDEN_HOST (#62): on Linux or macOS, aapt2
// 37.0.0 wraps sw2147483648dp to the default Config rather than stop it at sw65535dp, and writes
// other NaN bits, and the aapt2 tests miss nearly all of it, as they are deliberate rejections.
// The CPU in parentheses is only recorded.
TEST(SdkBaseline, Aapt2GoldenComesFromScriptPins) {
  std::map<std::string, std::string> pins;
  for (const char* name : {"BUILD_TOOLS", "PLATFORM", "GOLDEN_HOST"}) {
    ASSERT_NO_FATAL_FAILURE(readAapt2ScriptPin(name, &pins[name]));
  }
  const std::regex made(R"(build-tools (\S+) and (\S+) on (\w+) \(\w+\)\.)");
  for (const char* path : {HYDROID_AAPT2_VALUES, HYDROID_AAPT2_CONFIGS}) {
    std::ifstream in(path);
    ASSERT_TRUE(in) << "cannot open " << path;
    std::string header;
    std::getline(in, header);
    std::smatch m;
    ASSERT_TRUE(std::regex_search(header, m, made)) << path << ": " << header;
    EXPECT_EQ(m[1].str(), pins["BUILD_TOOLS"]) << path << ": build-tools";
    EXPECT_EQ(m[2].str(), pins["PLATFORM"]) << path << ": platform";
    EXPECT_EQ(m[3].str(), pins["GOLDEN_HOST"]) << path << ": OS";
  }
}

// README.md names the build-tools, platform and OS of the aapt2 golden files, to say what to
// install and where to run the scripts, so each of them must be what the script pins: raising a
// pin and making the files again without the README fails here (#66). Version.h only points to
// the pins, but any it names is held to them too. A README that no longer says one of them in
// these words fails, so that rewording it means changing the pattern here.
TEST(SdkBaseline, DocsNameScriptPins) {
  std::map<std::string, std::string> pins;
  for (const char* name : {"BUILD_TOOLS", "PLATFORM", "GOLDEN_HOST"}) {
    ASSERT_NO_FATAL_FAILURE(readAapt2ScriptPin(name, &pins[name]));
  }
  const std::pair<const char*, std::regex> mentions[] = {
      {"BUILD_TOOLS", std::regex(R"(build-tools\s+(\d+(?:\.\d+)*))", std::regex::icase)},
      {"PLATFORM", std::regex(R"((android-\d+(?:\.\d+)*))")},
      {"GOLDEN_HOST", std::regex(R"((?:made\s+on|`GOLDEN_HOST`,)\s+(\w+))")},
  };
  for (const auto& [path, mustName] :
       {std::pair{HYDROID_README, true}, std::pair{HYDROID_VERSION_HEADER, false}}) {
    std::ifstream in(path);
    ASSERT_TRUE(in) << "cannot open " << path;
    std::stringstream doc;
    doc << in.rdbuf();
    const std::string text = doc.str();
    for (const auto& [name, said] : mentions) {
      int named = 0;
      for (std::sregex_iterator m(text.begin(), text.end(), said), end; m != end; ++m) {
        ++named;
        EXPECT_EQ((*m)[1].str(), pins[name]) << path << ": " << (*m)[0].str() << " is not " << name;
      }
      if (mustName) EXPECT_NE(named, 0) << path << " does not name " << name;
    }
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

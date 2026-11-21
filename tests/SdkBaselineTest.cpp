// Every reference hydroid is checked against comes from the API level of kEmulatedSdkVersion
// (#14): the golden dump's emulator, the golden app's compileSdk and targetSdk, the build-tools
// and platform the aapt2 golden files were made with, the release the AOSP framework res/
// folder list comes from, and the release and commit the matching code and locale tables come
// from. Raising one without the rest fails here.

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
// The AOSP branch of that level: API 37 is Android 17.
const std::string kRelease = "android" + std::to_string(kEmulatedSdkVersion - 20) + "-release";

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

// The value a generator script gives `name`, which it must set once, on a line of its own:
// `NAME = "..."`. Python keeps the last of two, so any other line that starts with the name and
// then =, :, +=, a comma or the like fails too, even below the first. One inside a block, or with
// the name second in `A, NAME = ...`, is not seen. Read a line at a time, as
// std::regex::multiline is not in every standard library.
void readScriptPin(const char* script, const std::string& name, std::string* value) {
  std::ifstream in(script);
  ASSERT_TRUE(in) << "cannot open " << script;
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
  ASSERT_NE(assignments, 0) << name << " not found in " << script;
  ASSERT_EQ(assignments, 1) << name << " is set " << assignments << " times in " << script;
}

void readAapt2ScriptPin(const std::string& name, std::string* value) {
  readScriptPin(HYDROID_AAPT2_SCRIPT, name, value);
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
// on Windows (AMD64). ...", after "# " in a text file and in the "generated" field, first, of a
// JSON one. All three must be what the script pins, so that changing a pin without
// making the files again fails too (#64). The OS is GOLDEN_HOST (#62): on Linux or macOS, aapt2
// 37.0.0 wraps sw2147483648dp to the default Config rather than stop it at sw65535dp, and writes
// other NaN bits. The aapt2 tests fail on those, as they pin what aapt2 makes of each deliberate
// rejection (#61), but only the header says which OS that is. The CPU in parentheses is only
// recorded.
TEST(SdkBaseline, Aapt2GoldenComesFromScriptPins) {
  std::map<std::string, std::string> pins;
  for (const char* name : {"BUILD_TOOLS", "PLATFORM", "GOLDEN_HOST"}) {
    ASSERT_NO_FATAL_FAILURE(readAapt2ScriptPin(name, &pins[name]));
  }
  const std::regex made(R"(build-tools (\S+) and (\S+) on (\w+) \(\w+\)\.)");
  for (const char* path :
       {HYDROID_AAPT2_VALUES, HYDROID_AAPT2_CONFIGS, HYDROID_AAPT2_TABLE, HYDROID_AAPT2_APP_TABLE,
        HYDROID_AAPT2_TABLE_CASES, HYDROID_AAPT2_STRINGS}) {
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
// the pins, but any it names is held to them too. The build-tools may follow a space, the ; of
// sdkmanager, the / or \ of a path or a backtick. A README that no longer says one of them in
// these words fails, so that rewording it means changing the pattern here.
TEST(SdkBaseline, DocsNameScriptPins) {
  std::map<std::string, std::string> pins;
  for (const char* name : {"BUILD_TOOLS", "PLATFORM", "GOLDEN_HOST"}) {
    ASSERT_NO_FATAL_FAILURE(readAapt2ScriptPin(name, &pins[name]));
  }
  const std::pair<const char*, std::regex> mentions[] = {
      {"BUILD_TOOLS", std::regex(R"(build-tools[\s;/\\`]+(\d+(?:\.\d+)*))", std::regex::icase)},
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
      if (mustName) {
        EXPECT_NE(named, 0) << path << " does not name " << name;
      }
    }
  }
}

// The branch and commit golden/aosp lists the folders of: "... in AOSP, android17-release at" and
// "# <commit> (...)" on the next line.
void readAospFolderListRelease(std::string* branch, std::string* commit) {
  std::ifstream in(HYDROID_AOSP_RES_DIRS);
  ASSERT_TRUE(in) << "cannot open " << HYDROID_AOSP_RES_DIRS;
  std::string first;
  std::string second;
  std::getline(in, first);
  std::getline(in, second);
  const std::string header = first + "\n" + second;
  const std::regex release(R"((android\d+-release) at\n# ([0-9a-f]{40}) )");
  std::smatch m;
  ASSERT_TRUE(std::regex_search(header, m, release)) << header;
  *branch = m[1].str();
  *commit = m[2].str();
}

TEST(SdkBaseline, AospFolderListIsOfEmulatedLevel) {
  std::string branch;
  std::string commit;
  ASSERT_NO_FATAL_FAILURE(readAospFolderListRelease(&branch, &commit));
  EXPECT_EQ(branch, kRelease);
}

// gen_locale_data.py pins the android<N>-release that ConfigMatch.cpp and LocaleData.cpp were
// ported from, and the commit of it that LocaleDataTables.inc is made from (#215). Both are the
// release and commit golden/aosp lists the framework folders of, so that raising the level, or
// moving golden/aosp to another commit, without raising BRANCH and COMMIT fails here.
TEST(SdkBaseline, LocaleScriptPinsEmulatedRelease) {
  std::string branch;
  std::string commit;
  ASSERT_NO_FATAL_FAILURE(readScriptPin(HYDROID_LOCALE_SCRIPT, "BRANCH", &branch));
  ASSERT_NO_FATAL_FAILURE(readScriptPin(HYDROID_LOCALE_SCRIPT, "COMMIT", &commit));
  EXPECT_EQ(branch, kRelease) << "BRANCH";
  std::string aospBranch;
  std::string aospCommit;
  ASSERT_NO_FATAL_FAILURE(readAospFolderListRelease(&aospBranch, &aospCommit));
  EXPECT_EQ(branch, aospBranch) << "BRANCH is not the release of " << HYDROID_AOSP_RES_DIRS;
  EXPECT_EQ(commit, aospCommit) << "COMMIT is not the commit of " << HYDROID_AOSP_RES_DIRS;
}

// LocaleDataTables.inc names the release and commit it was made from, "// <branch> at <commit>,",
// which must be what gen_locale_data.py pins: changing a pin without making the tables again
// fails here (#215). Only the header is read, as making the tables again downloads from
// android.googlesource.com: the locale tables job of the full CI compares them with what the
// script makes of that commit (#228).
TEST(SdkBaseline, LocaleTablesComeFromScriptPins) {
  std::string branch;
  std::string commit;
  ASSERT_NO_FATAL_FAILURE(readScriptPin(HYDROID_LOCALE_SCRIPT, "BRANCH", &branch));
  ASSERT_NO_FATAL_FAILURE(readScriptPin(HYDROID_LOCALE_SCRIPT, "COMMIT", &commit));
  std::ifstream in(HYDROID_LOCALE_TABLES);
  ASSERT_TRUE(in) << "cannot open " << HYDROID_LOCALE_TABLES;
  const std::regex made(R"(// (android\d+-release) at ([0-9a-f]{40}),)");
  int named = 0;
  for (std::string line; std::getline(in, line) && line.rfind("//", 0) == 0;) {
    std::smatch m;
    if (!std::regex_match(line, m, made)) continue;
    ++named;
    EXPECT_EQ(m[1].str(), branch) << HYDROID_LOCALE_TABLES << ": BRANCH";
    EXPECT_EQ(m[2].str(), commit) << HYDROID_LOCALE_TABLES << ": COMMIT";
  }
  EXPECT_EQ(named, 1) << HYDROID_LOCALE_TABLES
                      << " does not name, once in its header, the release and commit it is of";
}

// The port itself is not read, only what each file says it was ported from: the code ported from
// AOSP (ConfigMatch.cpp, LocaleData.h, ConfigParser.cpp) and the tests ported with it name the
// release of BRANCH, as do README.md and Version.h, and any other android<N>-release they name is
// held to it too. Raising BRANCH without them fails here (#215), so that a new release is not
// taken without looking at the code that says it follows the old one.
TEST(SdkBaseline, PortNamesScriptRelease) {
  std::string branch;
  ASSERT_NO_FATAL_FAILURE(readScriptPin(HYDROID_LOCALE_SCRIPT, "BRANCH", &branch));
  const std::regex release(R"(android\d+-release)");
  for (const char* relative :
       {"src/core/ConfigMatch.cpp", "src/core/LocaleData.h", "src/core/ConfigParser.cpp",
        "tests/ConfigMatchTest.cpp", "tests/ConfigParserTest.cpp", "README.md",
        "include/hydroid/res/Version.h"}) {
    const std::string path = std::string(HYDROID_SOURCE_DIR) + "/" + relative;
    std::ifstream in(path);
    ASSERT_TRUE(in) << "cannot open " << path;
    std::stringstream file;
    file << in.rdbuf();
    const std::string text = file.str();
    int named = 0;
    for (std::sregex_iterator m(text.begin(), text.end(), release), end; m != end; ++m) {
      ++named;
      EXPECT_EQ(m->str(), branch) << path << ": " << m->str() << " is not BRANCH";
    }
    EXPECT_NE(named, 0) << path << " does not name the release it follows";
  }
}

}  // namespace
}  // namespace hydroid::res

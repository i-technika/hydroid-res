// Step 3 completion check: every folder of AOSP's framework res/ parses, and hydroid's folder name
// parser agrees with aapt2 on thousands of names: golden/aapt2/configs.tsv, written by
// scripts/golden/gen_aapt2_configs.py from the SDK's aapt2.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "hydroid/res/ConfigParser.h"

namespace hydroid::res {
namespace {

struct Row {
  std::string qualifiers;
  std::optional<std::string> aapt2;  // nullopt: aapt2 rejected the name
};

std::vector<Row> loadRows() {
  std::ifstream in(HYDROID_AAPT2_CONFIGS);
  EXPECT_TRUE(in) << "cannot open " << HYDROID_AAPT2_CONFIGS;
  std::vector<Row> rows;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '#') continue;
    // qualifiers \t configuration|-; either may be empty (the default configuration).
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) {
      ADD_FAILURE() << "bad row: " << line;
      continue;
    }
    Row row{line.substr(0, tab), line.substr(tab + 1)};
    if (row.aapt2 == "-") row.aapt2.reset();
    rows.push_back(std::move(row));
  }
  return rows;
}

const std::vector<Row>& rows() {
  static const std::vector<Row> r = loadRows();
  return r;
}

// Names aapt2 takes and hydroid rejects on purpose, and what aapt2 makes of each; see the top of
// ConfigParser.h. Every one must still be in the golden file as aapt2 took it then, and rejected
// by hydroid, so the list cannot go stale. SdkBaseline takes any build-tools of the level, so a
// golden made with another that writes one of these otherwise fails here too (#61). What aapt2
// makes of them is as on the host of the golden file, Windows: on Linux and macOS it rejects some,
// 2147483648x1 among them, and makes others of more (see ConfigParser.h).
// clang-format off
const std::map<std::string, std::string> kDeliberateRejects = {
    // Past 16 bits, which aapt2 wraps: sw65536dp is sw0dp, the default, and sw65537dp sw1dp, a
    // real one other than what was written. Past 0x7fffffff, 37.0.0 on Windows stops there first
    // (see ConfigParser.h). All but the last three come from numeric_qualifiers.tsv, and any
    // more of it that aapt2 takes fails ParsesLikeAapt2 until it is here.
    {"sw65536dp", ""}, {"sw65537dp", "sw1dp-v13"}, {"sw2147483647dp", "sw65535dp-v13"},
    {"sw2147483648dp", "sw65535dp-v13"}, {"sw4294967296dp", "sw65535dp-v13"},
    {"sw99999999999dp", "sw65535dp-v13"}, {"w65536dp", ""}, {"w65537dp", "w1dp-v13"},
    {"w2147483647dp", "w65535dp-v13"}, {"w2147483648dp", "w65535dp-v13"},
    {"w4294967296dp", "w65535dp-v13"}, {"w99999999999dp", "w65535dp-v13"}, {"h65536dp", ""},
    {"h65537dp", "h1dp-v13"}, {"h2147483647dp", "h65535dp-v13"}, {"h2147483648dp", "h65535dp-v13"},
    {"h4294967296dp", "h65535dp-v13"}, {"h99999999999dp", "h65535dp-v13"}, {"65536dpi", ""},
    {"65537dpi", "1dpi-v4"}, {"2147483647dpi", "nodpi-v4"}, {"2147483648dpi", "nodpi-v4"},
    {"4294967296dpi", "nodpi-v4"}, {"99999999999dpi", "nodpi-v4"}, {"65537x1", "1x1"},
    {"2147483647x1", "65535x1"}, {"2147483648x1", "65535x1"}, {"4294967296x1", "65535x1"},
    {"99999999999x1", "65535x1"}, {"1x65536", "1x0"}, {"1x65537", "1x1"}, {"65536x0", ""},
    {"65536x", ""}, {"131072x0", ""},
    // b+ with an empty subtag.
    {"b+", ""}, {"b++en", "en"}, {"b+sr+Latn+RS+", "b+sr+Latn+RS"},
    {"b+sr+Latn++x", "b+sr+Latn+X\\x00"}, {"b+zh+Hant+TW+", "b+zh+Hant+TW"},
    {"b+zh+Hant++", "b+zh+Hant"}, {"b+de+DE+1901+", "de-rDE"},
    // A language of 1 or of 4 and more letters, or not of letters.
    {"b+e", "e\\x00"}, {"b+english", "eng"}, {"b+e1", "e1"}, {"b+e1+US", "e1-rUS"},
    // A region of 1 character or 3 letters.
    {"b+en+abc", "en-rABC"}, {"b+en+xyz", "en-r89;"}, {"b+en+aaa", "en-rAAA"},
    {"b+sr+Latn+R+x", "b+sr+Latn+R\\x00"}, {"b+sr+Latn+abc+x", "b+sr+Latn+ABC+x"},
    // Of four subtags, a second of 3 is the region (#225), and the two after it are dropped.
    {"b+sr+Lat+RS+x", "sr-rLA4"}, {"b+de+abc+x+y", "de-rABC"},
    // A script that is not 4 characters, or a variant past 8.
    {"b+sr+Latnx+RS+x", "b+sr+Latn+RS+x"},
    {"b+de+CH+19011901x", "b+de+CH+19011901"}, {"b+de+CH+abcdefghi", "b+de+CH+abcdefgh"},
    {"b+sr+Latn+RS+abcdefghi", "b+sr+Latn+RS+abcdefgh"},
    // A character that is not an ASCII letter or digit.
    {"en-r+x", "en-r+X"},
};
// clang-format on

TEST(Aapt2Config, ParsesLikeAapt2) {
  ASSERT_GT(rows().size(), 5000u);
  std::set<std::string> deliberate;
  for (const Row& row : rows()) {
    const std::optional<Config> c = parseConfig(row.qualifiers);
    const std::string where = '"' + row.qualifiers + '"';
    const auto listed = kDeliberateRejects.find(row.qualifiers);
    if (row.aapt2 && !c && listed != kDeliberateRejects.end()) {
      deliberate.insert(row.qualifiers);
      EXPECT_EQ(*row.aapt2, listed->second) << where << ": aapt2 makes it something else now";
      continue;
    }
    if (c.has_value() != row.aapt2.has_value()) {
      ADD_FAILURE() << where << ": aapt2 "
                    << (row.aapt2 ? "takes it as \"" + *row.aapt2 + '"' : "rejects it")
                    << ", hydroid " << (c ? "takes it as \"" + toString(*c) + '"' : "rejects it");
    } else if (c) {
      EXPECT_EQ(toString(*c), *row.aapt2) << where;
    }
  }
  for (const auto& entry : kDeliberateRejects) {
    EXPECT_TRUE(deliberate.count(entry.first))
        << '"' << entry.first << "\" is no longer a deliberate rejection";
  }
}

// The names of scripts/golden/numeric_qualifiers.tsv, each template with each value, and that
// value.
std::vector<std::pair<std::string, uint64_t>> numericEdges() {
  std::ifstream in(HYDROID_NUMERIC_QUALIFIERS);
  EXPECT_TRUE(in) << "cannot open " << HYDROID_NUMERIC_QUALIFIERS;
  std::map<std::string, std::vector<std::string>> table;
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream cells(line);
    std::string key;
    cells >> key;
    for (std::string cell; cells >> cell;) table[key].push_back(cell);
  }
  std::vector<std::pair<std::string, uint64_t>> edges;
  for (const std::string& t : table["templates"]) {
    const size_t at = t.find("{}");
    if (at == std::string::npos) {
      ADD_FAILURE() << "template without {}: " << t;
      continue;
    }
    for (const std::string& v : table["values"]) {
      edges.emplace_back(t.substr(0, at) + v + t.substr(at + 2), std::stoull(v));
    }
  }
  return edges;
}

// Every name of numeric_qualifiers.tsv is in the golden file, so a template or a value added
// there without making it again fails, and hydroid rejects each past 65535, whatever aapt2 makes
// of it (#61). ParsesLikeAapt2 checks the rest: the same as aapt2 up to 65535, and past it each
// that aapt2 takes in kDeliberateRejects.
TEST(Aapt2Config, NumbersPast16Bits) {
  const std::vector<std::pair<std::string, uint64_t>> edges = numericEdges();
  ASSERT_GE(edges.size(), 49u);
  std::set<std::string> golden;
  for (const Row& row : rows()) golden.insert(row.qualifiers);
  for (const auto& [name, value] : edges) {
    EXPECT_TRUE(golden.count(name)) << name << " is not in configs.tsv: run gen_aapt2_configs.py";
    if (value > 65535) {
      EXPECT_FALSE(parseConfig(name)) << name;
    }
  }
}

// Whether toString writes `c` in a form aapt2 prints but no folder can be named, so that it cannot
// read back: an mcc below 100 without its leading zeros (mcc001 is mcc1; an mnc may have 1 to 3
// digits), kMncZero as mnc65535, a 3-digit region in the legacy form (b+es+419 is es-r419), and
// the language "any" with no mnc before it, where it reads back as the wildcard of the mcc, or of
// the mnc after an mcc.
bool writtenAsNoFolderIs(const Config& c) {
  const bool threeDigitRegion = c.region[2] >= '0' && c.region[2] <= '9';
  return (c.mcc != 0 && c.mcc < 100) || c.mnc == kMncZero ||
         (threeDigitRegion && c.script[0] == '\0' && c.variant[0] == '\0') ||
         (c.mnc == 0 && c.lang[0] == 'a' && c.lang[1] == 'n' && c.lang[2] == 'y');
}

// Every name hydroid takes reads back as the same Config from what toString makes of it, field by
// field (#54): ParsesLikeAapt2 compares strings only, and toString would hide a field it does not
// write. toString of that is the same string again. Configs that writtenAsNoFolderIs are left
// out; any other that does not read back fails.
TEST(Aapt2Config, ReadsBackFromToString) {
  int roundTrips = 0;
  int leftOut = 0;
  for (const Row& row : rows()) {
    const std::optional<Config> c = parseConfig(row.qualifiers);
    if (!c) continue;
    if (writtenAsNoFolderIs(*c)) {
      ++leftOut;
      continue;
    }
    const std::string where = '"' + row.qualifiers + '"';
    const std::string written = toString(*c);
    const std::optional<Config> back = parseConfig(written);
    if (!back) {
      ADD_FAILURE() << where << " is written \"" << written << "\", which does not read back";
      continue;
    }
    ++roundTrips;
    EXPECT_EQ(*back, *c) << where << " is written \"" << written << '"';
    EXPECT_EQ(toString(*back), written) << where;
  }
  EXPECT_GT(roundTrips, 4800) << "left out " << leftOut;
}

// The golden file reaches every qualifier, and both outcomes.
TEST(Aapt2Config, CoversEveryQualifier) {
  std::map<std::string, int> seen;
  for (const Row& row : rows()) {
    const std::optional<Config> c = parseConfig(row.qualifiers);
    if (!c) {
      seen["rejected"]++;
      continue;
    }
    seen["accepted"]++;
    if (c->mcc) seen["mcc"]++;
    if (c->mnc) seen["mnc"]++;
    if (c->lang[0]) seen["lang"]++;
    if (c->region[0]) seen["region"]++;
    if (c->script[0]) seen["script"]++;
    if (c->variant[0]) seen["variant"]++;
    if (c->grammaticalGender != GrammaticalGender::Any) seen["grammaticalGender"]++;
    if (c->layoutDir != LayoutDir::Any) seen["layoutDir"]++;
    if (c->smallestWidthDp) seen["smallestWidthDp"]++;
    if (c->widthDp) seen["widthDp"]++;
    if (c->heightDp) seen["heightDp"]++;
    if (c->screenSize != ScreenSize::Any) seen["screenSize"]++;
    if (c->screenLong != ScreenLong::Any) seen["screenLong"]++;
    if (c->screenRound != ScreenRound::Any) seen["screenRound"]++;
    if (c->wideColorGamut != WideColorGamut::Any) seen["wideColorGamut"]++;
    if (c->hdr != Hdr::Any) seen["hdr"]++;
    if (c->orientation != Orientation::Any) seen["orientation"]++;
    if (c->uiModeType != UiModeType::Any) seen["uiModeType"]++;
    if (c->uiModeNight != UiModeNight::Any) seen["uiModeNight"]++;
    if (c->density) seen["density"]++;
    if (c->touchscreen != Touchscreen::Any) seen["touchscreen"]++;
    if (c->keysHidden != KeysHidden::Any) seen["keysHidden"]++;
    if (c->keyboard != Keyboard::Any) seen["keyboard"]++;
    if (c->navHidden != NavHidden::Any) seen["navHidden"]++;
    if (c->navigation != Navigation::Any) seen["navigation"]++;
    if (c->screenWidth || c->screenHeight) seen["screenPixels"]++;
    if (c->sdkVersion) seen["sdkVersion"]++;
    if (c->minorVersion) seen["minorVersion"]++;
  }
  for (const char* field : {"rejected",
                            "accepted",
                            "mcc",
                            "mnc",
                            "lang",
                            "region",
                            "script",
                            "variant",
                            "grammaticalGender",
                            "layoutDir",
                            "smallestWidthDp",
                            "widthDp",
                            "heightDp",
                            "screenSize",
                            "screenLong",
                            "screenRound",
                            "wideColorGamut",
                            "hdr",
                            "orientation",
                            "uiModeType",
                            "uiModeNight",
                            "density",
                            "touchscreen",
                            "keysHidden",
                            "keyboard",
                            "navHidden",
                            "navigation",
                            "screenPixels",
                            "sdkVersion",
                            "minorVersion"}) {
    EXPECT_GE(seen[field], 3) << field;
  }
}

std::vector<std::string> aospFrameworkDirs() {
  std::ifstream in(HYDROID_AOSP_RES_DIRS);
  EXPECT_TRUE(in) << "cannot open " << HYDROID_AOSP_RES_DIRS;
  std::vector<std::string> dirs;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] != '#') dirs.push_back(line);
  }
  return dirs;
}

TEST(AospFrameworkRes, EveryFolderParses) {
  const std::vector<std::string> dirs = aospFrameworkDirs();
  EXPECT_EQ(dirs.size(), 2849u);
  const std::set<std::string> kTypes = {"anim",         "animator",   "color",  "drawable",
                                        "interpolator", "layout",     "menu",   "mipmap",
                                        "raw",          "transition", "values", "xml"};
  std::map<std::string, int> types;
  for (const std::string& dir : dirs) {
    const std::optional<ResDir> d = parseResDir(dir);
    ASSERT_TRUE(d) << dir;
    EXPECT_TRUE(kTypes.count(d->type)) << dir;
    types[d->type]++;
  }
  EXPECT_EQ(types["values"], 2763);
  EXPECT_EQ(types["drawable"], 28);
}

// Each folder is a row of configs.tsv that aapt2 took, so ParsesLikeAapt2 has checked it: a list
// updated without rerunning gen_aapt2_configs.py fails here.
void expectInAapt2Golden(const std::vector<std::string>& dirs) {
  std::map<std::string, const Row*> byQualifiers;
  for (const Row& row : rows()) byQualifiers.emplace(row.qualifiers, &row);
  for (const std::string& dir : dirs) {
    const size_t dash = dir.find('-');
    const auto it = byQualifiers.find(dash == std::string::npos ? "" : dir.substr(dash + 1));
    if (it == byQualifiers.end()) {
      ADD_FAILURE() << dir << " is not in configs.tsv";
    } else {
      EXPECT_TRUE(it->second->aapt2) << dir << ": aapt2 rejects it";
    }
  }
}

TEST(AospFrameworkRes, EveryFolderIsInAapt2Golden) { expectInAapt2Golden(aospFrameworkDirs()); }

// A few of them as aapt2 writes them; ParsesLikeAapt2 checks all.
TEST(AospFrameworkRes, Samples) {
  auto canonical = [](const char* dir) {
    const std::optional<ResDir> d = parseResDir(dir);
    return d ? d->type + " " + toString(d->config) : "rejected";
  };
  EXPECT_EQ(canonical("values"), "values ");
  EXPECT_EQ(canonical("drawable-sw600dp-hdpi"), "drawable sw600dp-hdpi-v13");
  EXPECT_EQ(canonical("values-mcc310-mnc004"), "values mcc310-mnc4");
  EXPECT_EQ(canonical("values-b+sr+Latn-television"), "values b+sr+Latn-television-v8");
  EXPECT_EQ(canonical("values-fr-feminine"), "values fr-feminine-v34");
  EXPECT_EQ(canonical("layout-round-watch"), "layout round-watch-v23");
  EXPECT_EQ(canonical("mipmap-watch-anydpi"), "mipmap watch-anydpi-v21");
  EXPECT_EQ(canonical("values-mcc334-mnc020-fr-rCA-feminine"),
            "values mcc334-mnc20-fr-rCA-feminine-v34");
  EXPECT_EQ(canonical("values-sw1500dp-110dpi"), "values sw1500dp-110dpi-v13");
  EXPECT_EQ(canonical("drawable-w240dp"), "drawable w240dp-v13");
  EXPECT_EQ(canonical("values-en-rXC-watch"), "values en-rXC-watch-v8");
}

// The golden app's own folders, which the device dump in golden/data comes from.
TEST(GoldenAppRes, EveryFolderParses) {
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(HYDROID_TEST_RES_DIR)) {
    if (!entry.is_directory()) continue;
    count++;
    EXPECT_TRUE(parseResDir(entry.path().filename().string())) << entry.path().filename().string();
  }
  EXPECT_GT(count, 10);
}

TEST(GoldenAppRes, EveryFolderIsInAapt2Golden) {
  std::vector<std::string> dirs;
  for (const auto& entry : std::filesystem::directory_iterator(HYDROID_TEST_RES_DIR)) {
    if (entry.is_directory()) dirs.push_back(entry.path().filename().string());
  }
  EXPECT_GT(dirs.size(), 10u);
  expectInAapt2Golden(dirs);
}

}  // namespace
}  // namespace hydroid::res

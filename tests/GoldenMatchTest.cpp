// Golden check for matching: for every device condition of golden/data, the value hydroid picks
// of each resource of the golden app against the one Android resolved on the emulator, and the
// locale it reads the resources in. The pick_ strings of the app's values*/match.xml say which
// folder they are in, so a value picked from another folder shows as its name.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "GoldenUtil.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"

namespace hydroid::res {
namespace {

using test::forEachGoldenCondition;
using test::GoldenDevice;
using test::goldenDevice;

constexpr const char* kPackage = "com.itechnika.hydroid.golden";

constexpr int kTypeString = 3;       // TypedValue.TYPE_STRING
constexpr int kTypeDimension = 5;    // TypedValue.TYPE_DIMENSION
constexpr int kTypeFraction = 6;     // TypedValue.TYPE_FRACTION
constexpr int kTypeIntBoolean = 18;  // TypedValue.TYPE_INT_BOOLEAN

// The golden app's res/, as hydroid loads it.
const ResourceTable& table() {
  static const ResourceTable* const t = [] {
    auto* loaded = new ResourceTable;
    std::vector<Diagnostic> errors;
    EXPECT_TRUE(loaded->addPackage(HYDROID_TEST_RES_DIR, kPackage, kAppPackageId, &errors))
        << (errors.empty() ? std::string() : errors[0].message);
    return loaded;
  }();
  return *t;
}

// A value as TypedValue has it: its type and data, and the text of a string, whose data is only
// its place in a pool.
struct Resolved {
  int type = 0;
  uint32_t data = 0;
  std::string text;
};

// The value `resources` picks of `id`, through the references to other resources of the app,
// each picked for the same configuration, as Resources.getValue(id, value, true) follows them.
// Following references is hydroid's own in step 6 (#26).
std::optional<Resolved> resolve(const Resources& resources, ResId id) {
  for (int depth = 0; depth < 20; depth++) {
    const Candidate* candidate = resources.candidate(id);
    if (candidate == nullptr) return std::nullopt;
    const Value& v = candidate->value;
    switch (v.kind()) {
      case ValueKind::Reference: id = v.ref(); continue;
      case ValueKind::String:
        return Resolved{kTypeString, 0, table().package(kPackage)->strings.at(v.index())};
      case ValueKind::Color: return Resolved{static_cast<int>(v.colorFormat()), v.argb(), {}};
      case ValueKind::Dimension: return Resolved{kTypeDimension, v.complexData(), {}};
      case ValueKind::Fraction: return Resolved{kTypeFraction, v.complexData(), {}};
      case ValueKind::Int:
        return Resolved{static_cast<int>(v.intFormat()), static_cast<uint32_t>(v.intValue()), {}};
      case ValueKind::Bool: return Resolved{kTypeIntBoolean, v.boolValue() ? 0xffffffffu : 0u, {}};
      default: return std::nullopt;
    }
  }
  return std::nullopt;
}

// A locale without telling a language's old code from its new one: Android's Configuration
// names Hebrew he, and its resources iw.
std::string localeKey(const Config& c) {
  Config locale;
  locale.lang = c.lang;
  static constexpr std::pair<const char*, const char*> kOldAndNew[] = {
      {"iw", "he"}, {"in", "id"}, {"ji", "yi"}};
  for (const auto& [oldCode, newCode] : kOldAndNew) {
    if (std::string(c.lang.data()) == oldCode) locale.lang = {newCode[0], newCode[1]};
  }
  locale.region = c.region;
  locale.script = c.script;
  locale.variant = c.variant;
  return toString(locale);
}

struct Stats {
  size_t conditions = 0;
  size_t values = 0;
  // By pick_ string, the texts Android returned under some condition.
  std::map<std::string, std::set<std::string>> picked;
};

// Checks every condition, once, and returns what it saw.
const Stats& checkedGolden() {
  static const Stats stats = [] {
    Stats out;
    forEachGoldenCondition([&](const std::string& condition, const nlohmann::json& golden) {
      SCOPED_TRACE(condition);
      out.conditions++;
      const GoldenDevice device = goldenDevice(golden.at("condition"));
      Resources resources(table());
      resources.setConfiguration(device.config, device.locales);

      // The locale Android put first is the one it read the resources in.
      const std::string locales =
          golden.at("condition").at("configuration").at("locales").get<std::string>();
      const std::optional<Config> first = parseLanguageTag(locales.substr(0, locales.find(',')));
      ASSERT_TRUE(first) << locales;
      EXPECT_EQ(localeKey(resources.configuration()), localeKey(*first))
          << "of " << golden.at("condition").at("requestedLocales");

      for (const auto& [key, v] : golden.at("values").items()) {
        SCOPED_TRACE(key);
        out.values++;
        const size_t slash = key.find('/');
        const std::optional<ResType> type = parseResType(key.substr(0, slash));
        ASSERT_TRUE(type);
        const std::optional<ResId> id = table().find(kPackage, *type, key.substr(slash + 1));
        ASSERT_TRUE(id);
        // The id aapt2 gave it in the app.
        EXPECT_EQ(*id, std::stoul(v.at("id").get<std::string>(), nullptr, 16));

        const std::optional<Resolved> resolved = resolve(resources, *id);
        ASSERT_TRUE(resolved) << "no value picked";
        const int goldenType = v.at("type").get<int>();
        EXPECT_EQ(resolved->type, goldenType);
        if (goldenType == kTypeString) {
          const std::string text = v.at("string").get<std::string>();
          EXPECT_EQ(resolved->text, text);
          if (key.rfind("string/pick_", 0) == 0) out.picked[key.substr(slash + 1)].insert(text);
        } else {
          EXPECT_EQ(resolved->data, static_cast<uint32_t>(v.at("data").get<int64_t>()));
        }
      }
    });
    return out;
  }();
  return stats;
}

// The completion criterion of step 5: hydroid picks what Android picked, under every condition.
TEST(GoldenMatch, PicksWhatAndroidPicked) {
  const Stats& stats = checkedGolden();
  // The matrix of 7 densities x 3 languages x night x orientation x 3 smallest widths, and the
  // extras of GoldenDumpTest.kt, one for each thing the matrix does not vary.
  EXPECT_EQ(stats.conditions, 252u + 123u);
  // Every resource of the app's values, under every condition.
  size_t resources = 0;
  for (const TypeTable& type : table().package(kPackage)->types) resources += type.entries.size();
  EXPECT_EQ(stats.values, stats.conditions * resources);
}

// Every value of a pick_ string is there to be picked under some condition: one that never is
// checks nothing. Those that Android never picks are listed with the reason.
TEST(GoldenMatch, EveryFolderIsPickedSomewhere) {
  const Stats& stats = checkedGolden();
  std::map<std::string, std::set<std::string>> never;
  const Package& package = *table().package(kPackage);
  for (const Entry& entry : package.type(ResType::String)->entries) {
    if (entry.name.rfind("pick_", 0) != 0) continue;
    const auto picked = stats.picked.find(entry.name);
    for (const Candidate& candidate : entry.candidates) {
      const std::string& text = package.strings.at(candidate.value.index());
      if (picked == stats.picked.end() || picked->second.count(text) == 0) {
        never[entry.name].insert(text);
      }
    }
  }
  const std::map<std::string, std::set<std::string>> kNeverPicked = {
      // The emulator always says what its screen, its colors and its input are, so a value for
      // one of their states is always there to take.
      {"pick_gamut", {"default"}},
      {"pick_hdr", {"default"}},
      {"pick_keyboard", {"default"}},
      {"pick_keys_hidden", {"default"}},
      {"pick_layout_dir", {"default"}},
      {"pick_long", {"default"}},
      {"pick_navigation", {"default"}},
      {"pick_night", {"default"}},
      {"pick_orientation", {"default"}},
      {"pick_round", {"default"}},
      {"pick_size", {"default"}},
      {"pick_touch", {"default"}},
      // No density is mdpi, and values-mdpi/ comes after values/ in the table.
      {"pick_density", {"default"}},
      // anydpi is better than any density.
      {"pick_density_any", {"default", "mdpi", "nodpi", "xhdpi"}},
      // Hebrew and Indonesian are iw and in to Android's resources: values-he/ and values-id/
      // are of no device.
      {"pick_he", {"he"}},
      {"pick_id", {"id"}},
      // Android gives its resources no navigationHidden.
      {"pick_nav_hidden", {"navexposed", "navhidden"}},
      // The emulator's display is larger than 800x480, and smaller than 4000x3000.
      {"pick_pixels", {"4000x3000", "default"}},
      // Every condition is of API 37: v30 and v37 always match, v38 never does. The app is built
      // for API 24 and up, so aapt2 leaves out what v21 replaces there, the value of values/.
      {"pick_precedence", {"default"}},
      {"pick_version", {"default", "v21", "v30", "v38"}},
  };
  EXPECT_EQ(never, kNeverPicked);
}

}  // namespace
}  // namespace hydroid::res

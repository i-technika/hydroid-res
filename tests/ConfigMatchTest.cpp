// matches and isBetterThan, which pick a resource's value for a device as Android's
// ResTable_config::match and isBetterThan do. The cases marked AOSP are those of AOSP's own
// tests of them (frameworks/base libs/androidfw/tests/Config_test.cpp and ConfigLocale_test.cpp,
// android17-release), with their comments; GoldenMatchTest checks the same functions against what
// an emulator picked.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"

namespace hydroid::res {
namespace {

// The Config of the qualifiers of a folder name, with the version aapt2 gives them.
Config cfg(const std::string& qualifiers) {
  const std::optional<Config> c = parseConfig(qualifiers);
  EXPECT_TRUE(c) << qualifiers;
  return c.value_or(Config{});
}

// A locale as AOSP's tests fill one in (fillIn), nullptr for a part it has not.
Config locale(const char* lang, const char* region = nullptr, const char* script = nullptr,
              const char* variant = nullptr) {
  Config c;
  if (lang != nullptr) std::copy_n(lang, std::strlen(lang), c.lang.begin());
  if (region != nullptr) std::copy_n(region, std::strlen(region), c.region.begin());
  if (script != nullptr) std::copy_n(script, std::strlen(script), c.script.begin());
  if (variant != nullptr) std::copy_n(variant, std::strlen(variant), c.variant.begin());
  return c;
}

Config density(int dpi, int sdkVersion = 4) {
  Config c;
  c.density = static_cast<uint16_t>(dpi);
  c.sdkVersion = static_cast<uint16_t>(sdkVersion);
  return c;
}

// AOSP's selectBest: the best of `configs` for `device`, starting from the default Config.
Config selectBest(const Config& device, const std::vector<Config>& configs) {
  Config best;
  for (const Config& c : configs) {
    if (matches(c, device) && isBetterThan(c, best, device)) best = c;
  }
  return best;
}

// ---- AOSP: Config_test.cpp -------------------------------------------------------------------

TEST(ConfigMatchAosp, SelectsBestDensity) {
  Config device;
  device.density = kDensityXhdpi;
  device.sdkVersion = 21;

  std::vector<Config> configs;
  Config expected = density(kDensityHdpi);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);

  expected = density(kDensityXxhdpi);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);

  expected = density(kDensityXxhdpi - 20);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);

  configs.push_back(density(kDensityHdpi + 20));
  EXPECT_EQ(selectBest(device, configs), expected);

  configs.push_back(density(kDensityXhdpi - 1));
  EXPECT_EQ(selectBest(device, configs), expected);

  expected = density(kDensityXhdpi);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);

  expected = density(kDensityAny, 21);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);
}

TEST(ConfigMatchAosp, SelectsBestDensityWhenNoneSpecified) {
  Config device;
  device.sdkVersion = 21;

  std::vector<Config> configs;
  configs.push_back(density(kDensityHdpi));
  Config expected = density(kDensityMdpi);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);

  expected = density(kDensityAny);
  configs.push_back(expected);
  EXPECT_EQ(selectBest(device, configs), expected);
}

TEST(ConfigMatchAosp, MatchesRoundQualifier) {
  Config device;
  Config round;
  round.screenRound = ScreenRound::Yes;
  EXPECT_FALSE(matches(round, device));
  device.screenRound = ScreenRound::Yes;
  EXPECT_TRUE(matches(round, device));
  device.screenRound = ScreenRound::No;
  EXPECT_FALSE(matches(round, device));

  Config notRound;
  notRound.screenRound = ScreenRound::No;
  EXPECT_TRUE(matches(notRound, device));
}

TEST(ConfigMatchAosp, RoundIsMoreSpecific) {
  Config device;
  device.screenRound = ScreenRound::Yes;
  device.screenLong = ScreenLong::Yes;

  const Config a;
  Config b = a;
  b.screenLong = ScreenLong::Yes;
  Config c = b;
  c.screenRound = ScreenRound::Yes;
  EXPECT_TRUE(isBetterThan(b, a, device));
  EXPECT_TRUE(isBetterThan(c, b, device));
}

TEST(ConfigMatchAosp, SdkAndMinorVersionMatch) {
  // The resource's version, then the platform's.
  const struct {
    const char* resource;
    const char* platform;
    bool matches;
  } kCases[] = {
      {"", "v41", true},         {"", "v41.1", true},       {"v41", "v41", true},
      {"v41", "v41.1", true},    {"v41", "v41.2", true},    {"v41", "v42", true},
      {"v41", "v42.1", true},    {"v41.1", "v41", false},   {"v41.1", "v41.1", true},
      {"v41.1", "v41.2", true},  {"v41.1", "v42", true},    {"v41.1", "v42.1", true},
      {"v41.2", "v41", false},   {"v41.2", "v41.1", false}, {"v41.2", "v41.2", true},
      {"v41.2", "v42", true},    {"v41.2", "v42.1", true},  {"v42", "v41", false},
      {"v42", "v41.1", false},   {"v42", "v41.2", false},   {"v42", "v42", true},
      {"v42", "v42.1", true},    {"v42.1", "v41", false},   {"v42.1", "v41.1", false},
      {"v42.1", "v41.2", false}, {"v42.1", "v42", false},   {"v42.1", "v42.1", true},
  };
  for (const auto& c : kCases) {
    EXPECT_EQ(matches(cfg(c.resource), cfg(c.platform)), c.matches)
        << c.resource << " on " << c.platform;
  }

  // No folder is v0.3, which is of no version.
  Config config;
  config.minorVersion = 3;
  EXPECT_FALSE(matches(config, cfg("v41")));
}

TEST(ConfigMatchAosp, SdkAndMinorVersionIsBetterThan) {
  for (const char* platform : {"v45", "v45.9"}) {
    SCOPED_TRACE(platform);
    const Config requested = cfg(platform);
    EXPECT_FALSE(isBetterThan(cfg("v40"), cfg("v40"), requested));
    EXPECT_TRUE(isBetterThan(cfg("v41"), cfg("v40"), requested));
    EXPECT_TRUE(isBetterThan(cfg("v41.1"), cfg("v41"), requested));
    EXPECT_TRUE(isBetterThan(cfg("v41.2"), cfg("v41.1"), requested));
    EXPECT_TRUE(isBetterThan(cfg("v42"), cfg("v41.2"), requested));
    EXPECT_TRUE(isBetterThan(cfg("v43.1"), cfg("v42"), requested));
  }
}

TEST(ConfigMatchAosp, SdkAndMinorVersionSelectBest) {
  const auto best = [](const char* platform, const std::vector<const char*>& names) {
    std::vector<Config> configs;
    for (const char* name : names) configs.push_back(cfg(name));
    return toString(selectBest(cfg(platform), configs));
  };
  for (const char* platform : {"v45", "v45.9"}) {
    SCOPED_TRACE(platform);
    EXPECT_EQ(best(platform, {"v40", "v42", "v41"}), "v42");
    EXPECT_EQ(best(platform, {"v40.5", "v42.3", "v41.2"}), "v42.3");
    EXPECT_EQ(best(platform, {"v42.5", "v42.3", "v41.2"}), "v42.5");
    EXPECT_EQ(best(platform, {"v42.3", "v41.2", "v42.5"}), "v42.5");
    EXPECT_EQ(best(platform, {"v42", "v42.5", "v41.2"}), "v42.5");
    EXPECT_EQ(best(platform, {"v42.5", "v42.3", "v44"}), "v44");
  }
  EXPECT_EQ(best("v45.9", {"v45.3", "", "v45.6"}), "v45.6");
}

// ---- AOSP: ConfigLocale_test.cpp -------------------------------------------------------------

// The script matches works out for a locale that names none, of those AOSP's test asks for: the
// one a device of that locale must name for the locale to match it. "" where it is not known,
// and the regions decide instead, so that any script matches.
std::string computedScript(const char* lang, const char* region) {
  std::string found;
  int count = 0;
  for (const char* script : {"Hant", "Hans", "Latn", "Arab", "Xpeo"}) {
    if (matches(locale(lang, region), locale(lang, region, script))) {
      found = script;
      count++;
    }
  }
  return count == 1 ? found : count == 0 ? "another" : "";
}

TEST(ConfigMatchAosp, ComputesScript) {
  EXPECT_EQ(computedScript("zh", "TW"), "Hant");
  EXPECT_EQ(computedScript("zh", "CN"), "Hans");
  EXPECT_EQ(computedScript("az", nullptr), "Latn");
  EXPECT_EQ(computedScript("az", "AZ"), "Latn");
  EXPECT_EQ(computedScript("az", "IR"), "Arab");
  EXPECT_EQ(computedScript("peo", nullptr), "Xpeo");
  EXPECT_EQ(computedScript("qaa", nullptr), "");
}

TEST(ConfigMatchAosp, LocaleMatch) {
  // Empty locale matches everything (as a default).
  EXPECT_TRUE(matches(locale(nullptr), locale("fr", "CA")));
  // Different languages don't match.
  EXPECT_FALSE(matches(locale("en", "CA"), locale("fr", "CA")));
  // Equivalent languages match.
  EXPECT_TRUE(matches(locale("tl", "PH"), locale("fil", "PH")));
  // If we can't infer the scripts, different regions don't match.
  EXPECT_FALSE(matches(locale("qaa", "FR"), locale("qaa", "CA")));
  // If we can't infer any of the scripts, different regions don't match.
  EXPECT_FALSE(matches(locale("qaa", "FR", "Latn"), locale("qaa", "CA")));
  EXPECT_FALSE(matches(locale("qaa", "FR"), locale("qaa", "CA", "Latn")));
  // Language-only resources still support language+region requests, even if we can't infer the
  // script.
  EXPECT_TRUE(matches(locale("qaa"), locale("qaa", "CA")));
  // Even if we can't infer the scripts, exactly equal locales match.
  EXPECT_TRUE(matches(locale("qaa", "CA"), locale("qaa", "CA")));
  // If the resolved scripts are the same, it doesn't matter if they were explicitly provided or
  // not, and they match.
  EXPECT_TRUE(matches(locale("az"), locale("az", nullptr, "Latn")));
  // If the resolved scripts are different, they don't match.
  EXPECT_FALSE(matches(locale("az"), locale("az", nullptr, "Cyrl")));
  EXPECT_FALSE(matches(locale("az"), locale("az", "IR")));
  // If the resolved scripts are the same, it doesn't matter if they were explicitly provided or
  // not, and they match.
  EXPECT_TRUE(matches(locale("az", "IR"), locale("az", nullptr, "Arab")));
  // en-XA is a pseudo-locale, and English resources are not a match for it.
  EXPECT_FALSE(matches(locale("en"), locale("en", "XA")));
  // en-XA is a pseudo-locale, and its resources don't support English locales.
  EXPECT_FALSE(matches(locale("en", "XA"), locale("en")));
  // Even if they are pseudo-locales, exactly equal locales match.
  EXPECT_TRUE(matches(locale("en", "XA"), locale("en", "XA")));
  // ar-XB is a pseudo-locale, and Arabic resources are not a match for it.
  EXPECT_FALSE(matches(locale("ar"), locale("ar", "XB")));
  // ar-XB is a pseudo-locale, and its resources don't support Arabic locales.
  EXPECT_FALSE(matches(locale("ar", "XB"), locale("ar")));
  // Even if they are pseudo-locales, exactly equal locales match.
  EXPECT_TRUE(matches(locale("ar", "XB"), locale("ar", "XB")));
  // match_emptyScript: a resource without a script, as every one aapt2 makes of a folder that
  // names none.
  EXPECT_TRUE(matches(locale("fr", "FR"), locale("fr", "CA")));
}

// isLocaleBetterThan(config1, config2, request), each way.
struct LocaleCase {
  Config request;
  Config config1;
  Config config2;
  bool firstIsBetter;  // false: neither is
};

void expectLocaleCases(const std::vector<LocaleCase>& cases) {
  for (const LocaleCase& c : cases) {
    SCOPED_TRACE(testing::Message() << c.config1 << " and " << c.config2 << " for " << c.request);
    EXPECT_EQ(isBetterThan(c.config1, c.config2, c.request), c.firstIsBetter);
    EXPECT_FALSE(isBetterThan(c.config2, c.config1, c.request));
  }
}

TEST(ConfigMatchAosp, LocaleIsBetterThanBasics) {
  expectLocaleCases({
      {locale(nullptr), locale("fr", "FR"), locale("fr", "CA"), false},
      {locale("fr", "CA"), locale(nullptr), locale(nullptr), false},
      {locale("fr", "CA"), locale("fr", "FR"), locale(nullptr), true},
      {locale("de", "DE"), locale("de", "DE"), locale("de", "DE", nullptr, "1901"), true},
      {locale("de", "DE"), locale("de", "DE", nullptr, "1901"), locale("de", "DE", nullptr, "1996"),
       false},
      {locale("de", "DE", nullptr, "1901"), locale("de", "DE", nullptr, "1901"), locale("de", "DE"),
       true},
      {locale("de", "DE", nullptr, "1901"), locale("de", "DE", nullptr, "1996"), locale("de", "DE"),
       false},
      {locale("fil", "PH"), locale("tl", "PH"), locale("fil", "US"), true},
      {locale("fil", "PH", nullptr, "fonipa"), locale("tl", "PH", nullptr, "fonipa"),
       locale("fil", "PH"), true},
      {locale("fil", "PH"), locale("fil", "PH"), locale("tl", "PH"), true},
  });
}

TEST(ConfigMatchAosp, LocaleIsBetterThanRegionComparison) {
  // The language, the requested region, then two regions of which the first is the better;
  // nullptr for none.
  const struct {
    const char* lang;
    const char* requested;
    const char* region1;
    const char* region2;
    const char* script = nullptr;  // of all three
    bool firstIsBetter = true;
  } kCases[] = {
      // Both supported locales are the same, so none is better than the other.
      {"es", "AR", "419", "419", nullptr, false},
      // An exact locale match is better than a parent.
      {"es", "AR", "AR", "419"},
      // A closer parent is better.
      {"es", "AR", "419", nullptr},
      // A parent is better than a non-parent representative locale.
      {"es", "AR", "419", "ES"},
      {"es", "AR", nullptr, "ES"},
      // A closer locale is better.
      {"es", "AR", "PE", "ES"},
      // Special case for Latin American Spanish: es-MX and es-US are pseudo-parents of all Latin
      // American Spanish locales.
      {"es", "AR", "US", nullptr},
      {"es", "MX", "US", nullptr},
      {"es", "AR", "MX", nullptr},
      {"es", "US", "MX", nullptr},
      // Even though es-MX and es-US are pseudo-parents of all Latin American Spanish locales,
      // es-419 is a closer parent.
      {"es", "AR", "419", "MX"},
      {"es", "US", "419", "MX"},
      {"es", "MX", "419", "US"},
      // Special case for Latin American Spanish: es-MX and es-US are pseudo-parents of all Latin
      // American Spanish locales.
      {"es", "AR", "MX", "BO"},
      {"es", "AR", "US", "BO"},
      // A representative locale is better if they are equidistant.
      {"es", "IC", "ES", "GQ"},
      // If all is equal, the locale earlier in the dictionary is better.
      {"es", "AR", "MX", "US"},
      // If all is equal, the locale earlier in the dictionary is better and letters are better
      // than numbers.
      {"es", "GQ", "IC", "419"},
      // A closer parent is better.
      {"en", "GB", "001", nullptr},
      // A parent is better than a non-parent.
      {"en", "PR", nullptr, "001"},
      // A closer parent is better.
      {"en", "DE", "150", "001"},
      // A closer locale is better.
      {"en", "IN", "AU", "US"},
      {"en", "PR", "001", "GB"},
      // A representative locale is better if they are equidistant.
      {"en", "IN", "GB", "AU"},
      // If all is equal, the locale earlier in the dictionary is better.
      {"en", "IN", "AU", "CA"},
      // A closer parent is better.
      {"pt", "MZ", "PT", nullptr},
      // A parent is better than a non-parent.
      {"pt", "MZ", "PT", "BR"},
      {"zh", "MO", "HK", "TW", "Hant"},
      // A representative locale is better if they are equidistant.
      {"zh", "US", "TW", "HK", "Hant"},
      // A closer parent is better.
      {"ar", "DZ", "015", nullptr},
      // A parent is better than a non-parent.
      {"ar", "EG", nullptr, "015"},
      // A representative locale is better if they are equidistant.
      {"ar", "QA", "EG", "BH"},
      // If all is equal, the locale earlier in the dictionary is better and letters are better
      // than numbers.
      {"ar", "QA", "SA", "015"},
  };
  std::vector<LocaleCase> cases;
  for (const auto& c : kCases) {
    cases.push_back({locale(c.lang, c.requested, c.script), locale(c.lang, c.region1, c.script),
                     locale(c.lang, c.region2, c.script), c.firstIsBetter});
  }
  expectLocaleCases(cases);
}

// Default resources are considered better matches for US English and US-like English locales
// than International English locales.
TEST(ConfigMatchAosp, UsEnglishIsSpecial) {
  expectLocaleCases({
      // Default is better than International English.
      {locale("en", "US"), locale(nullptr), locale("en", "001"), true},
      // Default is better than British English.
      {locale("en", "US"), locale(nullptr), locale("en", "GB"), true},
      // Even for Puerto Rico, default is better than International English.
      {locale("en", "PR"), locale(nullptr), locale("en", "001"), true},
      // "English" is better than default, since it's a parent of US English.
      {locale("en", "US"), locale("en"), locale(nullptr), true},
      // "English" is better than default, since it's a parent of Puerto Rico English.
      {locale("en", "PR"), locale("en"), locale(nullptr), true},
      // For US English itself, we prefer default to its siblings in the parent tree.
      {locale("en", "US"), locale(nullptr), locale("en", "PR"), true},
  });
}

// ---- each qualifier --------------------------------------------------------------------------

// Two values of each qualifier that must be the device's own.
constexpr const char* kExactQualifiers[][2] = {
    {"mcc310", "mcc311"},       {"mnc260", "mnc261"},
    {"feminine", "masculine"},  {"ldltr", "ldrtl"},
    {"long", "notlong"},        {"round", "notround"},
    {"widecg", "nowidecg"},     {"highdr", "lowdr"},
    {"port", "land"},           {"car", "desk"},
    {"night", "notnight"},      {"notouch", "finger"},
    {"keyshidden", "keyssoft"}, {"navexposed", "navhidden"},
    {"qwerty", "12key"},        {"dpad", "wheel"},
};

TEST(ConfigMatch, DefaultMatchesAnyDevice) {
  EXPECT_TRUE(matches(Config{}, Config{}));
  EXPECT_TRUE(matches(Config{}, cfg("mcc310-mnc260-ko-rKR-sw600dp-land-night-xhdpi-v37")));
}

TEST(ConfigMatch, ExactQualifiersMustBeTheDevices) {
  for (const auto& [one, other] : kExactQualifiers) {
    SCOPED_TRACE(one);
    EXPECT_TRUE(matches(cfg(one), cfg(one)));
    EXPECT_FALSE(matches(cfg(one), cfg(other)));
    EXPECT_FALSE(matches(cfg(other), cfg(one)));
    // A device that does not say is not one that has it.
    Config unknown;
    unknown.sdkVersion = 37;
    EXPECT_FALSE(matches(cfg(one), unknown));
  }
}

TEST(ConfigMatch, KeysExposedAlsoMatchesTheSoftKeyboard) {
  EXPECT_TRUE(matches(cfg("keysexposed"), cfg("keyssoft")));
  EXPECT_FALSE(matches(cfg("keyssoft"), cfg("keysexposed")));
  EXPECT_FALSE(matches(cfg("keysexposed"), cfg("keyshidden")));
  EXPECT_FALSE(matches(cfg("keyshidden"), cfg("keyssoft")));
}

TEST(ConfigMatch, SizesMatchUpToTheDevices) {
  const struct {
    const char* smaller;
    const char* device;
    const char* larger;
  } kCases[] = {
      {"sw320dp", "sw600dp", "sw601dp"}, {"w320dp", "w600dp", "w601dp"},
      {"h320dp", "h600dp", "h601dp"},    {"small", "normal", "large"},
      {"normal", "large", "xlarge"},     {"320x240", "640x480", "641x480"},
      {"320x240", "640x480", "640x481"}, {"v21", "v37", "v38"},
  };
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.device);
    EXPECT_TRUE(matches(cfg(c.smaller), cfg(c.device)));
    EXPECT_TRUE(matches(cfg(c.device), cfg(c.device)));
    EXPECT_FALSE(matches(cfg(c.larger), cfg(c.device)));
  }
  // The width and the height each on their own.
  EXPECT_TRUE(matches(cfg("w600dp-h400dp"), cfg("w600dp-h400dp")));
  EXPECT_FALSE(matches(cfg("w600dp-h401dp"), cfg("w600dp-h400dp")));
  EXPECT_FALSE(matches(cfg("w601dp-h400dp"), cfg("w600dp-h400dp")));
}

TEST(ConfigMatch, AnyDensityMatches) {
  for (const char* resource : {"ldpi", "xxxhdpi", "nodpi", "anydpi", "123dpi"}) {
    EXPECT_TRUE(matches(cfg(resource), cfg("hdpi-v37"))) << resource;
    Config noDensity;
    noDensity.sdkVersion = 37;
    EXPECT_TRUE(matches(cfg(resource), noDensity)) << resource;
  }
}

TEST(ConfigMatch, LocaleMatchesByScript) {
  // The region does not matter where the scripts are the same.
  EXPECT_TRUE(matches(cfg("en-rGB"), cfg("en-rAU")));
  EXPECT_TRUE(matches(cfg("ko-rKP"), cfg("ko-rKR")));
  EXPECT_TRUE(matches(cfg("es-rMX"), cfg("b+es+419")));
  // zh-TW and zh-HK are Hant, zh-CN and zh-SG Hans.
  EXPECT_TRUE(matches(cfg("zh-rTW"), cfg("zh-rHK")));
  EXPECT_FALSE(matches(cfg("zh-rCN"), cfg("zh-rHK")));
  EXPECT_TRUE(matches(cfg("zh-rCN"), cfg("zh-rSG")));
  EXPECT_TRUE(matches(cfg("b+zh+Hant"), cfg("zh-rTW")));
  EXPECT_FALSE(matches(cfg("b+zh+Hans"), cfg("zh-rTW")));
  // A script the device names is the device's, whatever its region.
  EXPECT_TRUE(matches(cfg("zh-rTW"), cfg("b+zh+Hant+CN")));
  EXPECT_FALSE(matches(cfg("zh-rCN"), cfg("b+zh+Hant+CN")));
  // sr is Cyrl, and sr-ME Latn.
  EXPECT_TRUE(matches(cfg("sr"), cfg("sr-rRS")));
  EXPECT_FALSE(matches(cfg("b+sr+Latn"), cfg("sr-rRS")));
  EXPECT_TRUE(matches(cfg("b+sr+Latn"), cfg("sr-rME")));
  EXPECT_FALSE(matches(cfg("sr"), cfg("sr-rME")));
  // The variant keeps nothing out.
  EXPECT_TRUE(matches(cfg("b+de+DE+1901"), cfg("de-rDE")));
  // A language is not its other code: resources go by the old one (Resources sees to a device's).
  EXPECT_TRUE(matches(cfg("iw"), cfg("iw-rIL")));
  EXPECT_FALSE(matches(cfg("he"), cfg("iw-rIL")));
  EXPECT_FALSE(matches(cfg("iw"), cfg("he-rIL")));
  // A device without a locale takes no resource of one.
  EXPECT_FALSE(matches(cfg("en"), Config{}));
}

TEST(ConfigMatch, MinorVersion) {
  EXPECT_TRUE(matches(cfg("v36.1"), cfg("v37")));
  EXPECT_FALSE(matches(cfg("v37.1"), cfg("v37")));
  EXPECT_TRUE(matches(cfg("v37.1"), cfg("v37.1")));
  EXPECT_TRUE(matches(cfg("v37.1"), cfg("v38")));
}

// ---- isBetterThan ----------------------------------------------------------------------------

TEST(ConfigIsBetterThan, NothingIsBetterThanItself) {
  const Config device = cfg("mcc310-mnc260-ko-rKR-sw600dp-w600dp-h800dp-port-night-xhdpi-v37");
  for (const char* q : {"", "ko", "ko-rKR", "sw600dp", "w600dp-h800dp", "port", "night", "xhdpi",
                        "anydpi", "nodpi", "v21"}) {
    EXPECT_FALSE(isBetterThan(cfg(q), cfg(q), device)) << q;
  }
}

TEST(ConfigIsBetterThan, GivingAQualifierIsBetterThanNot) {
  for (const auto& [one, other] : kExactQualifiers) {
    SCOPED_TRACE(one);
    const Config device = cfg(one);
    EXPECT_TRUE(isBetterThan(cfg(one), Config{}, device));
    EXPECT_FALSE(isBetterThan(Config{}, cfg(one), device));
  }
}

TEST(ConfigIsBetterThan, AQualifierTheDeviceDoesNotGiveDecidesNothing) {
  // Neither matches such a device, but for the version alone; isBetterThan passes them over.
  for (const auto& [one, other] : kExactQualifiers) {
    SCOPED_TRACE(one);
    Config with = cfg(one);
    with.sdkVersion = 0;
    EXPECT_FALSE(isBetterThan(with, Config{}, Config{}));
    EXPECT_FALSE(isBetterThan(Config{}, with, Config{}));
  }
}

// The qualifiers in the order of their precedence: one of each beats one of any that follows. It
// is the order of a folder name, but that both hidden states come before the keyboard.
TEST(ConfigIsBetterThan, EarlierQualifierDecides) {
  const std::vector<const char*> order = {
      "mcc310",      "mnc260",     "feminine", "ldltr", "sw320dp", "w320dp", "large", "long",
      "round",       "widecg",     "highdr",   "port",  "car",     "night",  "hdpi",  "finger",
      "keysexposed", "navexposed", "qwerty",   "dpad",  "640x480", "v37",
  };
  for (size_t i = 0; i + 1 < order.size(); i++) {
    for (size_t j = i + 1; j < order.size(); j++) {
      SCOPED_TRACE(std::string(order[i]) + " over " + order[j]);
      // A device with both, whichever comes first in a folder name.
      std::optional<Config> device = parseConfig(std::string(order[i]) + "-" + order[j]);
      if (!device) device = parseConfig(std::string(order[j]) + "-" + order[i]);
      ASSERT_TRUE(device);
      EXPECT_TRUE(isBetterThan(cfg(order[i]), cfg(order[j]), *device));
      EXPECT_FALSE(isBetterThan(cfg(order[j]), cfg(order[i]), *device));
    }
  }
}

// A locale comes after the mnc and before everything else, but only decides for the better.
TEST(ConfigIsBetterThan, LocaleDecidesOnlyForTheBetter) {
  const Config device = cfg("mcc310-ko-rKR-sw600dp-v37");
  EXPECT_TRUE(isBetterThan(cfg("mcc310"), cfg("ko"), device));
  EXPECT_FALSE(isBetterThan(cfg("ko"), cfg("mcc310"), device));

  // Each is better than the other: ko by its locale, sw600dp by its width, as the worse locale
  // does not end the comparison. Android takes the first of them it meets (bestCandidate).
  EXPECT_TRUE(isBetterThan(cfg("ko"), cfg("sw600dp"), device));
  EXPECT_TRUE(isBetterThan(cfg("sw600dp"), cfg("ko"), device));
}

TEST(ConfigIsBetterThan, LargerSmallestWidthAndVersion) {
  const Config device = cfg("sw720dp-v37");
  EXPECT_TRUE(isBetterThan(cfg("sw600dp"), cfg("sw320dp"), device));
  EXPECT_FALSE(isBetterThan(cfg("sw320dp"), cfg("sw600dp"), device));
  EXPECT_TRUE(isBetterThan(cfg("v30"), cfg("v21"), device));
  EXPECT_FALSE(isBetterThan(cfg("v21"), cfg("v30"), device));
  // The version is the last: sw320dp is v13, and still worse than sw600dp-v13.
  EXPECT_TRUE(isBetterThan(cfg("sw600dp"), cfg("sw320dp-v30"), device));
}

TEST(ConfigIsBetterThan, WidthAndHeightByTheirSum) {
  const Config device = cfg("w800dp-h600dp");
  EXPECT_TRUE(isBetterThan(cfg("w700dp"), cfg("w600dp"), device));
  EXPECT_TRUE(isBetterThan(cfg("h500dp"), cfg("h400dp"), device));
  // 100 short in width and none in height, against none and 300.
  EXPECT_TRUE(isBetterThan(cfg("w700dp-h600dp"), cfg("w800dp-h300dp"), device));
  EXPECT_FALSE(isBetterThan(cfg("w800dp-h300dp"), cfg("w700dp-h600dp"), device));
  // A dimension that is not given is as far as it can be.
  EXPECT_TRUE(isBetterThan(cfg("w600dp-h100dp"), cfg("w600dp"), device));
  // 800 short without a width, against 100 and 600 without a height.
  EXPECT_TRUE(isBetterThan(cfg("w700dp"), cfg("h600dp"), device));
  EXPECT_FALSE(isBetterThan(cfg("h600dp"), cfg("w700dp"), device));
  // The same sum: neither.
  EXPECT_FALSE(isBetterThan(cfg("w700dp-h600dp"), cfg("w800dp-h500dp"), device));
  EXPECT_FALSE(isBetterThan(cfg("w800dp-h500dp"), cfg("w700dp-h600dp"), device));
}

TEST(ConfigIsBetterThan, ScreenSizeCountsNoneAsNormal) {
  EXPECT_TRUE(isBetterThan(cfg("large"), cfg("normal"), cfg("xlarge")));
  EXPECT_TRUE(isBetterThan(cfg("large"), Config{}, cfg("xlarge")));
  // None is normal on a screen that is at least normal: small is worse, and normal the better
  // only for saying so.
  EXPECT_TRUE(isBetterThan(Config{}, cfg("small"), cfg("normal")));
  EXPECT_FALSE(isBetterThan(cfg("small"), Config{}, cfg("normal")));
  EXPECT_TRUE(isBetterThan(cfg("normal"), Config{}, cfg("large")));
  EXPECT_FALSE(isBetterThan(Config{}, cfg("normal"), cfg("large")));
  // On a small screen, small is better than none.
  EXPECT_TRUE(isBetterThan(cfg("small"), Config{}, cfg("small")));
  EXPECT_FALSE(isBetterThan(Config{}, cfg("small"), cfg("small")));
}

TEST(ConfigIsBetterThan, LayoutDirection) {
  // The larger value is the better, which only tells ldrtl from ldltr where both match, and
  // they never do.
  EXPECT_TRUE(isBetterThan(cfg("ldrtl"), cfg("ldltr"), cfg("ldrtl")));
  EXPECT_TRUE(isBetterThan(cfg("ldrtl"), Config{}, cfg("ldrtl")));
  EXPECT_TRUE(isBetterThan(cfg("ldltr"), Config{}, cfg("ldltr")));
}

// The density a device takes of two, none being mdpi.
int betterDensity(int a, int b, int device) {
  Config d;
  d.density = static_cast<uint16_t>(device);
  Config ca;
  ca.density = static_cast<uint16_t>(a);
  Config cb;
  cb.density = static_cast<uint16_t>(b);
  const bool aIsBetter = isBetterThan(ca, cb, d);
  const bool bIsBetter = isBetterThan(cb, ca, d);
  EXPECT_NE(aIsBetter, bIsBetter) << a << " and " << b << " for " << device;
  return aIsBetter ? a : b;
}

TEST(ConfigIsBetterThan, Density) {
  // Both below the device's: the higher.
  EXPECT_EQ(betterDensity(160, 240, 480), 240);
  // Both above: the lower.
  EXPECT_EQ(betterDensity(480, 640, 320), 480);
  // One below and one above: the higher, to scale down, however far it is.
  EXPECT_EQ(betterDensity(320, 640, 480), 640);
  EXPECT_EQ(betterDensity(160, 640, 240), 640);
  EXPECT_EQ(betterDensity(239, 640, 240), 640);
  // The device's own.
  EXPECT_EQ(betterDensity(240, 480, 240), 240);
  EXPECT_EQ(betterDensity(160, 240, 240), 240);
  // None is mdpi, for a resource and for a device.
  EXPECT_EQ(betterDensity(0, 120, 160), 0);
  EXPECT_EQ(betterDensity(0, 240, 240), 240);
  EXPECT_EQ(betterDensity(0, 240, 120), 0);
  EXPECT_EQ(betterDensity(120, 240, 0), 240);
  EXPECT_EQ(betterDensity(320, 240, 0), 240);
  // anydpi is better than any density; nodpi is only a very high one.
  EXPECT_EQ(betterDensity(kDensityAny, 240, 240), kDensityAny);
  EXPECT_EQ(betterDensity(kDensityAny, kDensityNone, 240), kDensityAny);
  EXPECT_EQ(betterDensity(kDensityNone, 240, 480), kDensityNone);
  EXPECT_EQ(betterDensity(kDensityNone, 640, 480), 640);
  EXPECT_EQ(betterDensity(kDensityNone, 480, 480), 480);
  // An anydpi device is an mdpi one.
  EXPECT_EQ(betterDensity(160, 240, kDensityAny), 160);
}

TEST(ConfigIsBetterThan, KeysHidden) {
  // keysexposed and keyssoft both match a keyssoft device: its own is the better.
  EXPECT_TRUE(isBetterThan(cfg("keyssoft"), cfg("keysexposed"), cfg("keyssoft")));
  EXPECT_FALSE(isBetterThan(cfg("keysexposed"), cfg("keyssoft"), cfg("keyssoft")));
  EXPECT_TRUE(isBetterThan(cfg("keysexposed"), Config{}, cfg("keyssoft")));
  EXPECT_FALSE(isBetterThan(Config{}, cfg("keysexposed"), cfg("keyssoft")));
}

TEST(ConfigIsBetterThan, ScreenInPixelsByItsSum) {
  const Config device = cfg("800x600");
  EXPECT_TRUE(isBetterThan(cfg("800x480"), cfg("640x480"), device));
  EXPECT_FALSE(isBetterThan(cfg("640x480"), cfg("800x480"), device));
  EXPECT_TRUE(isBetterThan(cfg("640x480"), Config{}, device));
  // The same width, so only the height tells them apart.
  EXPECT_TRUE(isBetterThan(cfg("800x480"), cfg("800x300"), device));
  EXPECT_FALSE(isBetterThan(cfg("800x300"), cfg("800x480"), device));
}

TEST(ConfigIsBetterThan, Locale) {
  // The language over none, the region closer to the device's, then the device's own.
  const Config device = cfg("es-rMX");
  EXPECT_TRUE(isBetterThan(cfg("es"), Config{}, device));
  EXPECT_TRUE(isBetterThan(cfg("b+es+419"), cfg("es"), device));
  EXPECT_TRUE(isBetterThan(cfg("es-rMX"), cfg("b+es+419"), device));
  EXPECT_TRUE(isBetterThan(cfg("b+es+419"), cfg("es-rES"), device));
  // And not the other way: a table meets each pair in one order only, but isBetterThan takes
  // either.
  EXPECT_FALSE(isBetterThan(cfg("es"), cfg("b+es+419"), device));
  EXPECT_FALSE(isBetterThan(cfg("b+es+419"), cfg("es-rMX"), device));
  EXPECT_FALSE(isBetterThan(cfg("es-rES"), cfg("b+es+419"), device));
  // Without a locale, a device takes none for better.
  EXPECT_FALSE(isBetterThan(cfg("es"), Config{}, Config{}));
  EXPECT_FALSE(isBetterThan(Config{}, cfg("es"), Config{}));
}

TEST(ConfigIsBetterThan, UsEnglishTakesItsOwnOverNone) {
  // On a US English device, none is better than the Englishes away from it (UsEnglishIsSpecial),
  // but values-en-rUS/ is still better than values/; AOSP's cases have no en-US candidate.
  const Config device = cfg("en-rUS");
  EXPECT_TRUE(isBetterThan(cfg("en-rUS"), Config{}, device));
  EXPECT_FALSE(isBetterThan(Config{}, cfg("en-rUS"), device));
  // So too for an English close to US English, with its own region.
  const Config puertoRico = cfg("en-rPR");
  EXPECT_TRUE(isBetterThan(cfg("en-rPR"), Config{}, puertoRico));
  EXPECT_FALSE(isBetterThan(Config{}, cfg("en-rPR"), puertoRico));
}

// ---- bestCandidate ---------------------------------------------------------------------------

// An entry with a value for each of `qualifiers`, in the order of a table.
Entry entryOf(const std::vector<const char*>& qualifiers) {
  Entry entry;
  for (const char* q : qualifiers) entry.candidates.push_back({cfg(q), Value{}});
  std::sort(entry.candidates.begin(), entry.candidates.end(),
            [](const Candidate& a, const Candidate& b) {
              return compareConfigs(a.config, b.config) < 0;
            });
  return entry;
}

std::string best(const std::vector<const char*>& qualifiers, const std::string& device) {
  const Entry entry = entryOf(qualifiers);
  const Candidate* c = bestCandidate(entry, cfg(device));
  return c != nullptr ? toString(c->config) : "none";
}

TEST(BestCandidate, PicksAmongThoseThatMatch) {
  const std::vector<const char*> q = {"",   "land",   "night", "sw600dp", "sw720dp-land",
                                      "ko", "ko-rKR", "xhdpi", "v30"};
  EXPECT_EQ(best(q, "v37"), "v30");
  EXPECT_EQ(best(q, "land-v37"), "land");
  EXPECT_EQ(best(q, "land-night-v37"), "land");
  EXPECT_EQ(best(q, "port-night-v37"), "night-v8");
  EXPECT_EQ(best(q, "sw600dp-land-night-v37"), "sw600dp-v13");
  EXPECT_EQ(best(q, "sw720dp-land-night-v37"), "sw720dp-land-v13");
  EXPECT_EQ(best(q, "sw720dp-port-night-v37"), "sw600dp-v13");
  EXPECT_EQ(best(q, "ko-rKR-sw720dp-land-night-xhdpi-v37"), "ko-rKR");
  EXPECT_EQ(best(q, "ko-rKP-sw720dp-land-v37"), "ko");
  EXPECT_EQ(best(q, "ja-rJP-port-xhdpi-v37"), "xhdpi-v4");
}

TEST(BestCandidate, NoneWhereNothingMatches) {
  EXPECT_EQ(best({"land", "ko"}, "port-v37"), "none");
  EXPECT_EQ(best({"v38"}, "v37"), "none");
  EXPECT_EQ(bestCandidate(Entry{}, Config{}), nullptr);
}

TEST(BestCandidate, DensityWithoutAMatchingOne) {
  const std::vector<const char*> q = {"ldpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi", ""};
  EXPECT_EQ(best(q, "ldpi-v37"), "ldpi-v4");
  // None is mdpi.
  EXPECT_EQ(best(q, "mdpi-v37"), "");
  EXPECT_EQ(best(q, "tvdpi-v37"), "hdpi-v4");
  EXPECT_EQ(best(q, "xxhdpi-v37"), "xxhdpi-v4");
  EXPECT_EQ(best(q, "560dpi-v37"), "xxxhdpi-v4");
  EXPECT_EQ(best({"ldpi", "xhdpi"}, "hdpi-v37"), "xhdpi-v4");
  EXPECT_EQ(best({"ldpi", "mdpi"}, "xxxhdpi-v37"), "mdpi-v4");
  EXPECT_EQ(best({"nodpi", "hdpi", "anydpi"}, "hdpi-v37"), "anydpi-v21");
  EXPECT_EQ(best({"nodpi", "ldpi"}, "hdpi-v37"), "nodpi-v4");
}

// isBetterThan is not an order, so the answer depends on the order of the candidates, which is
// compareConfigs'. On a British tablet, en-rGB has the better locale and en-rIN-sw600dp the
// better width, and each is better than the other. The table has en-rGB first, so Android holds
// it, asks whether en-rIN-sw600dp is better, and takes that: the locale, for all its
// precedence, loses to the width. Were they met the other way round, en-rGB would win.
TEST(BestCandidate, FollowsTheOrderOfTheTable) {
  const Entry entry = entryOf({"en-rGB", "en-rIN-sw600dp"});
  const Config device = cfg("en-rGB-sw600dp-v37");
  ASSERT_EQ(toString(entry.candidates[0].config), "en-rGB");
  ASSERT_TRUE(isBetterThan(entry.candidates[0].config, entry.candidates[1].config, device));
  ASSERT_TRUE(isBetterThan(entry.candidates[1].config, entry.candidates[0].config, device));
  EXPECT_EQ(bestCandidate(entry, device), &entry.candidates[1]);

  // Without a default to hold first, a locale that comes after a qualifier does win.
  EXPECT_EQ(best({"ko", "sw600dp"}, "ko-rKR-sw600dp-v37"), "ko");
}

// A device whose language tag has two variants keeps the last, as setBcp47Locale does. What
// AOSP's match and isBetterThan (android17-release) pick from the same candidates.
TEST(BestCandidate, DeviceTagWithTwoVariants) {
  const auto bestForTag = [](const std::vector<const char*>& qualifiers, const char* tag) {
    const Entry entry = entryOf(qualifiers);
    const std::optional<Config> device = parseLanguageTag(tag);
    EXPECT_TRUE(device) << tag;
    const Candidate* c = bestCandidate(entry, device.value_or(Config{}));
    return c != nullptr ? toString(c->config) : "none";
  };
  EXPECT_EQ(bestForTag({"b+de+DE+1901", "b+de+DE+1996"}, "de-DE-1901-1996"), "b+de+DE+1996");
  EXPECT_EQ(bestForTag({"b+sl+biske", "b+sl+rozaj"}, "sl-rozaj-biske"), "b+sl+biske");
  // The device's variant is "1996ncia", which valencia is not.
  EXPECT_EQ(bestForTag({"ca-rES", "b+ca+ES+valencia"}, "ca-ES-valencia-1996"), "ca-rES");
}

}  // namespace
}  // namespace hydroid::res

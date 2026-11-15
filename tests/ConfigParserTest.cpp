// Resource folder names -> Config, as aapt2 reads them. Aapt2ConfigTest checks the same parser
// against aapt2's own output (golden/aapt2/configs.tsv); these tests name the cases. Where hydroid
// rejects on purpose what aapt2 takes, the test says so.

#include <gtest/gtest.h>

#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "core/FieldCount.h"
#include "hydroid/res/ConfigParser.h"

namespace hydroid::res {
namespace {

// The canonical form of `qualifiers`, or "rejected".
std::string canonical(const char* qualifiers) {
  const std::optional<Config> c = parseConfig(qualifiers);
  return c ? toString(*c) : "rejected";
}

// ---- each qualifier ------------------------------------------------------------------------

TEST(ParseConfig, Default) {
  EXPECT_EQ(parseConfig(""), Config{});
  EXPECT_EQ(toString(Config{}), "");
}

TEST(ParseConfig, MccMnc) {
  Config c = *parseConfig("mcc310-mnc260");
  EXPECT_EQ(c.mcc, 310);
  EXPECT_EQ(c.mnc, 260);
  EXPECT_EQ(parseConfig("mnc260")->mnc, 260);  // an mnc needs no mcc
  // mnc0, mnc00 and mnc000 are all kMncZero, as 0 is "any", and leading zeros go.
  EXPECT_EQ(parseConfig("mnc00")->mnc, kMncZero);
  EXPECT_EQ(canonical("mnc000"), "mnc65535");
  EXPECT_EQ(canonical("mcc310-mnc004"), "mcc310-mnc4");
  // The mcc is 3 digits, not 0; the mnc 1 to 3.
  EXPECT_EQ(canonical("mcc31"), "rejected");
  EXPECT_EQ(canonical("mcc3100"), "rejected");
  EXPECT_EQ(canonical("mcc000"), "rejected");
  EXPECT_EQ(canonical("mnc1000"), "rejected");
}

TEST(ParseConfig, LegacyLocale) {
  Config c = *parseConfig("ko-rKR");
  EXPECT_EQ(std::string(c.lang.data()), "ko");
  EXPECT_EQ(std::string(c.region.data()), "KR");
  EXPECT_EQ(canonical("fil-rPH"), "fil-rPH");
  EXPECT_EQ(canonical("EN-rus"), "en-rUS");  // parts are lowercased, the region uppercased
  // Old language codes stay as written; telling iw from he is for matching (step 5).
  EXPECT_EQ(canonical("iw"), "iw");
  EXPECT_EQ(canonical("in"), "in");
  // The region needs its r, and two characters.
  EXPECT_EQ(canonical("en-US"), "rejected");
  EXPECT_EQ(canonical("en-rUSA"), "rejected");
  EXPECT_EQ(canonical("en-r419"), "rejected");
}

TEST(ParseConfig, Bcp47Locale) {
  Config c = *parseConfig("b+sr+Latn+RS");
  EXPECT_EQ(std::string(c.lang.data()), "sr");
  EXPECT_EQ(std::string(c.script.data(), 4), "Latn");
  EXPECT_EQ(std::string(c.region.data()), "RS");
  EXPECT_EQ(canonical("b+sr+Latn"), "b+sr+Latn");
  EXPECT_EQ(canonical("B+SR+LATN"), "b+sr+Latn");
  EXPECT_EQ(canonical("b+zh+Hant+TW"), "b+zh+Hant+TW");
  EXPECT_EQ(canonical("b+de+CH+1901"), "b+de+CH+1901");
  EXPECT_EQ(canonical("b+sl+rozaj"), "b+sl+rozaj");
  EXPECT_EQ(canonical("b+en+US+POSIX"), "b+en+US+posix");
  // Without a script or variant, the locale is written the legacy way, even where no folder
  // could be named so.
  EXPECT_EQ(canonical("b+en+US"), "en-rUS");
  EXPECT_EQ(canonical("b+es+419"), "es-r419");
  // Subtags are told apart by count and length: a 4-character second subtag is a script unless
  // it starts with a digit, and a short third one is the region, even after a region.
  EXPECT_EQ(canonical("b+en+1234"), "b+en+1234");
  EXPECT_EQ(canonical("b+sr+Lat+RS"), "sr-rRS");
  EXPECT_EQ(canonical("b+de+CH+19"), "de-r19");
  EXPECT_EQ(canonical("b+en+a"), "rejected");
  EXPECT_EQ(canonical("b+en+abcdefghi"), "rejected");
  // b+ is one part; a qualifier after it follows a '-'.
  EXPECT_EQ(canonical("b+en+US-ldrtl"), "en-rUS-ldrtl");
  EXPECT_EQ(canonical("b+en-rUS"), "rejected");
}

// aapt2 takes these, but cuts them short or packs them into other characters.
TEST(ParseConfig, Bcp47RejectsWhatAapt2Mangles) {
  for (const char* s : {"b+", "b+e", "b+english", "b++en", "b+en+xyz", "b+sr+Latn+R+x",
                        "b+sr+Latn++x", "b+sr+Lat+RS+x", "b+de+CH+19011901x", "en-r+x"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, AnyTwoOrThreeLettersAreALanguage) {
  // What names no qualifier at its place is tried as a language first.
  EXPECT_EQ(canonical("mcc"), "mcc");
  EXPECT_EQ(canonical("dpi"), "dpi");
  EXPECT_EQ(canonical("cat"), "cat");
  // But not car, which is a UI mode.
  EXPECT_EQ(canonical("car"), "car-v8");
  EXPECT_EQ(canonical("ca-car"), "ca-car-v8");
}

TEST(ParseConfig, GrammaticalGender) {
  EXPECT_EQ(parseConfig("feminine")->grammaticalGender, GrammaticalGender::Feminine);
  EXPECT_EQ(canonical("fr-masculine"), "fr-masculine-v34");
  EXPECT_EQ(canonical("neuter-ldrtl"), "neuter-ldrtl-v34");
  EXPECT_EQ(canonical("feminine-fr"), "rejected");
}

TEST(ParseConfig, ScreenDp) {
  Config c = *parseConfig("sw600dp-w720dp-h480dp");
  EXPECT_EQ(c.smallestWidthDp, 600);
  EXPECT_EQ(c.widthDp, 720);
  EXPECT_EQ(c.heightDp, 480);
  EXPECT_EQ(c.sdkVersion, 13);
  EXPECT_EQ(canonical("sw0dp"), "");  // 0 is any
  EXPECT_EQ(canonical("sw65535dp"), "sw65535dp-v13");
  // aapt2 wraps 65536 to 0 and 65537 to 1, so it takes sw65537dp as sw1dp. Past 0x7fffffff,
  // 37.0.0 on Windows stops there first: w99999999999dp is w65535dp. hydroid rejects them (see
  // ConfigParser.h, also for Linux and macOS). Aapt2Config.NumbersPast16Bits has the rest, every
  // number of scripts/golden/numeric_qualifiers.tsv.
  for (const char* s : {"sw65536dp", "sw65537dp", "sw2147483648dp", "w99999999999dp", "h65537dp"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
  for (const char* s : {"sw600", "sw600px", "sw-600dp", "SW600DPX", "swdp"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, NamedQualifiers) {
  Config c = *parseConfig(
      "ldrtl-large-long-round-widecg-highdr-land-television-night-finger-keyssoft-qwerty-"
      "navhidden-dpad");
  EXPECT_EQ(c.layoutDir, LayoutDir::Rtl);
  EXPECT_EQ(c.screenSize, ScreenSize::Large);
  EXPECT_EQ(c.screenLong, ScreenLong::Yes);
  EXPECT_EQ(c.screenRound, ScreenRound::Yes);
  EXPECT_EQ(c.wideColorGamut, WideColorGamut::Yes);
  EXPECT_EQ(c.hdr, Hdr::Yes);
  EXPECT_EQ(c.orientation, Orientation::Land);
  EXPECT_EQ(c.uiModeType, UiModeType::Television);
  EXPECT_EQ(c.uiModeNight, UiModeNight::Yes);
  EXPECT_EQ(c.touchscreen, Touchscreen::Finger);
  EXPECT_EQ(c.keysHidden, KeysHidden::Soft);
  EXPECT_EQ(c.keyboard, Keyboard::Qwerty);
  EXPECT_EQ(c.navHidden, NavHidden::Yes);
  EXPECT_EQ(c.navigation, Navigation::Dpad);
  EXPECT_EQ(canonical("small-notlong-notround-nowidecg-lowdr-port-desk-notnight-notouch-"
                      "keysexposed-nokeys-navexposed-nonav"),
            "small-notlong-notround-nowidecg-lowdr-port-desk-notnight-notouch-keysexposed-"
            "nokeys-navexposed-nonav-v26");
  EXPECT_EQ(canonical("square-stylus-12key-trackball"), "square-stylus-12key-trackball");
  EXPECT_EQ(canonical("xlarge-appliance-wheel"), "xlarge-appliance-wheel-v8");
  EXPECT_EQ(canonical("vrheadset"), "vrheadset-v26");
  EXPECT_EQ(canonical("watch-keyshidden"), "watch-keyshidden-v8");
  // "normal" is a screen size, not the normal UI mode, which has no qualifier: past the screen
  // size, only the UI mode could take it, and it does not (#55).
  EXPECT_EQ(canonical("normal"), "normal-v4");
  EXPECT_EQ(canonical("normal-car"), "normal-car-v8");
  for (const char* s : {"land-normal", "port-normal", "car-normal", "night-normal"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, Density) {
  EXPECT_EQ(parseConfig("ldpi")->density, kDensityLdpi);
  EXPECT_EQ(parseConfig("xxxhdpi")->density, kDensityXxxhdpi);
  EXPECT_EQ(parseConfig("tvdpi")->density, kDensityTvdpi);
  EXPECT_EQ(parseConfig("nodpi")->density, kDensityNone);
  EXPECT_EQ(parseConfig("anydpi")->density, kDensityAny);
  EXPECT_EQ(parseConfig("400dpi")->density, 400);
  // A number that has a name is written by its name, and 65534 and 65535 are anydpi and nodpi.
  EXPECT_EQ(canonical("480dpi"), "xxhdpi-v4");
  EXPECT_EQ(canonical("65534dpi"), "anydpi-v21");
  EXPECT_EQ(canonical("65535dpi"), "nodpi-v4");
  for (const char* s : {"0dpi", "-1dpi", "xxxxhdpi", "400dp"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
  // aapt2 wraps 65536 to 0 and 65537 to 1dpi. Past 0x7fffffff, 37.0.0 on Windows stops there
  // first: 2147483648dpi is 65535dpi, nodpi. hydroid rejects them (see ConfigParser.h), and the
  // rest of numeric_qualifiers.tsv (Aapt2Config.NumbersPast16Bits).
  for (const char* s : {"65536dpi", "65537dpi", "2147483648dpi"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, ScreenPixels) {
  Config c = *parseConfig("640x480");
  EXPECT_EQ(c.screenWidth, 640);
  EXPECT_EQ(c.screenHeight, 480);
  EXPECT_EQ(canonical("640x"), "640x0");  // aapt2 lets the height be empty
  EXPECT_EQ(canonical("0x0"), "");
  EXPECT_EQ(canonical("480x640"), "rejected");  // the width is the larger
  EXPECT_EQ(canonical("x480"), "rejected");
  EXPECT_EQ(canonical("640x480x2"), "rejected");
  EXPECT_EQ(canonical("65535x1"), "65535x1");
  // aapt2 wraps 65536 to 0, so it takes 65536x0 as the default and 1x65536 as 1x0, and 65537x1
  // as 1x1. Past 0x7fffffff, 37.0.0 on Windows stops there first: 99999999999x1 is 65535x1.
  // hydroid rejects them (see ConfigParser.h), and the rest of numeric_qualifiers.tsv
  // (Aapt2Config.NumbersPast16Bits).
  for (const char* s : {"65536x0", "65536x", "131072x0", "1x65536", "65537x1", "99999999999x1"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, Version) {
  EXPECT_EQ(parseConfig("v21")->sdkVersion, 21);
  EXPECT_EQ(canonical("v021"), "v21");
  EXPECT_EQ(canonical("v0"), "");
  EXPECT_EQ(canonical("v65535"), "v65535");
  // Unlike the other numbers, aapt2 rejects versions past 16 bits.
  EXPECT_EQ(canonical("v65536"), "rejected");
  EXPECT_EQ(canonical("v"), "rejected");
  EXPECT_EQ(canonical("21"), "rejected");
}

TEST(ParseConfig, MinorVersion) {
  Config c = *parseConfig("v36.1");
  EXPECT_EQ(c.sdkVersion, 36);
  EXPECT_EQ(c.minorVersion, 1);
  EXPECT_EQ(canonical("v37.2"), "v37.2");
  EXPECT_EQ(canonical("v36.01"), "v36.1");
  EXPECT_EQ(canonical("v36.0"), "v36");
  EXPECT_EQ(canonical("v35.0"), "v35");
  // Only from v36 on, and not on v0.
  EXPECT_EQ(canonical("v35.1"), "rejected");
  EXPECT_EQ(canonical("v0.1"), "rejected");
  EXPECT_EQ(canonical("v0.0"), "");
  for (const char* s : {"v36.", "v.1", "v36.1.1", "v36.65536", "v36.x"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

// ---- the implicit version ------------------------------------------------------------------

TEST(ParseConfig, VersionIsRaisedForNewerQualifiers) {
  EXPECT_EQ(canonical("hdpi"), "hdpi-v4");
  EXPECT_EQ(canonical("large"), "large-v4");
  EXPECT_EQ(canonical("night"), "night-v8");
  EXPECT_EQ(canonical("sw600dp"), "sw600dp-v13");
  EXPECT_EQ(canonical("anydpi"), "anydpi-v21");
  EXPECT_EQ(canonical("notround"), "notround-v23");
  EXPECT_EQ(canonical("lowdr"), "lowdr-v26");
  EXPECT_EQ(canonical("neuter"), "neuter-v34");
  // The newest qualifier decides, and a higher version given stays.
  EXPECT_EQ(canonical("sw600dp-night-hdpi"), "sw600dp-night-hdpi-v13");
  EXPECT_EQ(canonical("hdpi-v3"), "hdpi-v4");
  EXPECT_EQ(canonical("anydpi-v26"), "anydpi-v26");
  // Older qualifiers and the locale need no version.
  EXPECT_EQ(canonical("land"), "land");
  EXPECT_EQ(canonical("ldrtl"), "ldrtl");
  EXPECT_EQ(canonical("ko-rKR"), "ko-rKR");
  // Raising the version drops the minor version, and the v36 check comes after.
  EXPECT_EQ(canonical("anydpi-v4.1"), "anydpi-v21");
  EXPECT_EQ(canonical("feminine-v33.1"), "feminine-v34");
  EXPECT_EQ(canonical("feminine-v35.1"), "rejected");
}

// ---- the name as a whole -------------------------------------------------------------------

TEST(ParseConfig, Wildcard) {
  EXPECT_EQ(canonical("any"), "");
  EXPECT_EQ(canonical("any-hdpi"), "hdpi-v4");
  EXPECT_EQ(canonical("en-any"), "en");
  EXPECT_EQ(canonical("land-any"), "land");
  // In the place of the locale, "any" is the language any.
  EXPECT_EQ(canonical("any-any-any"), "any");
  // The grammatical gender takes no wildcard: this "any" is the layout direction's.
  EXPECT_EQ(canonical("feminine-any"), "feminine-v34");
}

TEST(ParseConfig, OrderAndRepeats) {
  for (const char* s : {"hdpi-land", "v21-hdpi", "en-mcc310", "night-car", "land-port", "hdpi-hdpi",
                        "ldrtl-en", "car-en"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, EmptyParts) {
  for (const char* s : {"-", "--", "hdpi-", "-hdpi", "hdpi--v21"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, UnknownParts) {
  for (const char* s : {"bogus", "land-hdpi-foo", "abcd", "e", "port-dark", "en_US"}) {
    EXPECT_EQ(canonical(s), "rejected") << s;
  }
}

TEST(ParseConfig, EveryQualifierAtOnce) {
  const char* all =
      "mcc310-mnc260-en-rUS-feminine-ldrtl-sw600dp-w720dp-h480dp-large-long-round-widecg-highdr-"
      "land-car-night-xhdpi-finger-keysexposed-qwerty-navexposed-dpad-640x480-v36.1";
  EXPECT_EQ(canonical(all), all);
}

// ---- folder names --------------------------------------------------------------------------

TEST(ParseResDir, SplitsAtTheFirstDash) {
  const std::optional<ResDir> d = parseResDir("values-ko-rKR");
  ASSERT_TRUE(d);
  EXPECT_EQ(d->type, "values");
  EXPECT_EQ(toString(d->config), "ko-rKR");
  EXPECT_EQ(parseResDir("drawable")->type, "drawable");
  EXPECT_EQ(parseResDir("drawable")->config, Config{});
  EXPECT_EQ(toString(parseResDir("mipmap-xxxhdpi")->config), "xxxhdpi-v4");
}

TEST(ParseResDir, TypeIsNotChecked) {
  EXPECT_EQ(parseResDir("Values-land")->type, "Values");
  EXPECT_EQ(parseResDir("-land")->type, "");
}

TEST(ParseResDir, TrailingDashIsTheDefault) {
  const std::optional<ResDir> d = parseResDir("values-");
  ASSERT_TRUE(d);
  EXPECT_EQ(d->type, "values");
  EXPECT_EQ(d->config, Config{});
}

TEST(ParseResDir, RejectsBadQualifiers) {
  EXPECT_FALSE(parseResDir("values-bogus"));
  EXPECT_FALSE(parseResDir("drawable-hdpi-land"));
  EXPECT_FALSE(parseResDir("values--"));
}

// ---- Config --------------------------------------------------------------------------------

TEST(Config, EqualityIsFieldByField) {
  EXPECT_EQ(parseConfig("hdpi"), parseConfig("hdpi-v4"));
  EXPECT_EQ(parseConfig("hdpi"), parseConfig("240dpi"));
  EXPECT_NE(parseConfig("hdpi"), parseConfig("hdpi-v5"));
  EXPECT_NE(parseConfig("en"), parseConfig("en-rUS"));
  EXPECT_NE(parseConfig("b+sr+Latn"), parseConfig("sr"));
  EXPECT_NE(parseConfig("v36"), parseConfig("v36.1"));
  Config a, b;
  b.uiModeType = UiModeType::Normal;
  EXPECT_NE(a, b);
}

TEST(Config, Prints) {
  std::ostringstream os;
  os << *parseConfig("en-rUS-sw600dp") << ' ' << Config{};
  EXPECT_EQ(os.str(), "Config(en-rUS-sw600dp-v13) Config()");
}

TEST(Config, DeviceValuesWithoutAQualifier) {
  // A device's UI mode is normal when it is none of the others. No folder names it.
  Config device;
  device.uiModeType = UiModeType::Normal;
  device.density = 420;
  EXPECT_EQ(toString(device), "420dpi");
}

// Each field alone makes a Config other than the default, so operator== compares each (#52). A
// field missing from Config.cpp's fields() fails its build; this catches one listed twice there
// in place of another, and a field added to Config fails this file's build until it is here.
TEST(Config, EveryFieldCountsForEquality) {
  const std::pair<const char*, void (*)(Config&)> kFields[] = {
      {"mcc", [](Config& c) { c.mcc = 310; }},
      {"mnc", [](Config& c) { c.mnc = 260; }},
      {"lang", [](Config& c) { c.lang = {'k', 'o'}; }},
      {"region", [](Config& c) { c.region = {'K', 'R'}; }},
      {"script", [](Config& c) { c.script = {'L', 'a', 't', 'n'}; }},
      {"variant", [](Config& c) { c.variant = {'p', 'o', 's', 'i', 'x'}; }},
      {"grammaticalGender", [](Config& c) { c.grammaticalGender = GrammaticalGender::Feminine; }},
      {"layoutDir", [](Config& c) { c.layoutDir = LayoutDir::Rtl; }},
      {"smallestWidthDp", [](Config& c) { c.smallestWidthDp = 600; }},
      {"widthDp", [](Config& c) { c.widthDp = 720; }},
      {"heightDp", [](Config& c) { c.heightDp = 480; }},
      {"screenSize", [](Config& c) { c.screenSize = ScreenSize::Large; }},
      {"screenLong", [](Config& c) { c.screenLong = ScreenLong::Yes; }},
      {"screenRound", [](Config& c) { c.screenRound = ScreenRound::Yes; }},
      {"wideColorGamut", [](Config& c) { c.wideColorGamut = WideColorGamut::Yes; }},
      {"hdr", [](Config& c) { c.hdr = Hdr::Yes; }},
      {"orientation", [](Config& c) { c.orientation = Orientation::Land; }},
      {"uiModeType", [](Config& c) { c.uiModeType = UiModeType::Normal; }},
      {"uiModeNight", [](Config& c) { c.uiModeNight = UiModeNight::Yes; }},
      {"density", [](Config& c) { c.density = kDensityHdpi; }},
      {"touchscreen", [](Config& c) { c.touchscreen = Touchscreen::Finger; }},
      {"keysHidden", [](Config& c) { c.keysHidden = KeysHidden::Soft; }},
      {"keyboard", [](Config& c) { c.keyboard = Keyboard::Qwerty; }},
      {"navHidden", [](Config& c) { c.navHidden = NavHidden::Yes; }},
      {"navigation", [](Config& c) { c.navigation = Navigation::Dpad; }},
      {"screenWidth", [](Config& c) { c.screenWidth = 640; }},
      {"screenHeight", [](Config& c) { c.screenHeight = 480; }},
      {"sdkVersion", [](Config& c) { c.sdkVersion = 21; }},
      {"minorVersion", [](Config& c) { c.minorVersion = 1; }},
  };
  static_assert(std::size(kFields) == detail::fieldCount<Config>(), "a Config field is missing");
  for (const auto& [name, set] : kFields) {
    Config c;
    set(c);
    EXPECT_FALSE(c == Config{}) << name;
    EXPECT_TRUE(c == c) << name;
  }
}

}  // namespace
}  // namespace hydroid::res

#include "hydroid/res/Config.h"

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>

#include "core/FieldCount.h"
#include "core/QualifierNames.h"
#include "hydroid/res/ConfigParser.h"

namespace hydroid::res {
namespace {

// Every value of the Config enums, and the density constants and kMncZero, is the one Android's
// AConfiguration gives it (#53), so an adapter or a binary table can copy them. The comment after
// each is the constant's name after ACONFIGURATION_, in the NDK's
// toolchains/llvm/prebuilt/<host>/sysroot/usr/include/android/configuration.h (r28c,
// 28.2.13676358). The compiler checks Config.h against the numbers here; with an NDK at hand,
// AConfiguration.MatchesNdk (scripts/check_aconfiguration.py) checks the numbers against the NDK,
// and that a value added to Config.h has an assert here too. Without one, nothing does.
template <typename E>
constexpr int value(E e) {
  return static_cast<int>(e);
}
static_assert(value(GrammaticalGender::Any) == 0);        // GRAMMATICAL_GENDER_ANY
static_assert(value(GrammaticalGender::Neuter) == 1);     // GRAMMATICAL_GENDER_NEUTER
static_assert(value(GrammaticalGender::Feminine) == 2);   // GRAMMATICAL_GENDER_FEMININE
static_assert(value(GrammaticalGender::Masculine) == 3);  // GRAMMATICAL_GENDER_MASCULINE

static_assert(value(LayoutDir::Any) == 0);  // LAYOUTDIR_ANY
static_assert(value(LayoutDir::Ltr) == 1);  // LAYOUTDIR_LTR
static_assert(value(LayoutDir::Rtl) == 2);  // LAYOUTDIR_RTL

static_assert(value(ScreenSize::Any) == 0);     // SCREENSIZE_ANY
static_assert(value(ScreenSize::Small) == 1);   // SCREENSIZE_SMALL
static_assert(value(ScreenSize::Normal) == 2);  // SCREENSIZE_NORMAL
static_assert(value(ScreenSize::Large) == 3);   // SCREENSIZE_LARGE
static_assert(value(ScreenSize::XLarge) == 4);  // SCREENSIZE_XLARGE

static_assert(value(ScreenLong::Any) == 0);  // SCREENLONG_ANY
static_assert(value(ScreenLong::No) == 1);   // SCREENLONG_NO
static_assert(value(ScreenLong::Yes) == 2);  // SCREENLONG_YES

static_assert(value(ScreenRound::Any) == 0);  // SCREENROUND_ANY
static_assert(value(ScreenRound::No) == 1);   // SCREENROUND_NO
static_assert(value(ScreenRound::Yes) == 2);  // SCREENROUND_YES

static_assert(value(WideColorGamut::Any) == 0);  // WIDE_COLOR_GAMUT_ANY
static_assert(value(WideColorGamut::No) == 1);   // WIDE_COLOR_GAMUT_NO
static_assert(value(WideColorGamut::Yes) == 2);  // WIDE_COLOR_GAMUT_YES

static_assert(value(Hdr::Any) == 0);  // HDR_ANY
static_assert(value(Hdr::No) == 1);   // HDR_NO
static_assert(value(Hdr::Yes) == 2);  // HDR_YES

static_assert(value(Orientation::Any) == 0);     // ORIENTATION_ANY
static_assert(value(Orientation::Port) == 1);    // ORIENTATION_PORT
static_assert(value(Orientation::Land) == 2);    // ORIENTATION_LAND
static_assert(value(Orientation::Square) == 3);  // ORIENTATION_SQUARE

static_assert(value(UiModeType::Any) == 0);         // UI_MODE_TYPE_ANY
static_assert(value(UiModeType::Normal) == 1);      // UI_MODE_TYPE_NORMAL
static_assert(value(UiModeType::Desk) == 2);        // UI_MODE_TYPE_DESK
static_assert(value(UiModeType::Car) == 3);         // UI_MODE_TYPE_CAR
static_assert(value(UiModeType::Television) == 4);  // UI_MODE_TYPE_TELEVISION
static_assert(value(UiModeType::Appliance) == 5);   // UI_MODE_TYPE_APPLIANCE
static_assert(value(UiModeType::Watch) == 6);       // UI_MODE_TYPE_WATCH
static_assert(value(UiModeType::VrHeadset) == 7);   // UI_MODE_TYPE_VR_HEADSET

static_assert(value(UiModeNight::Any) == 0);  // UI_MODE_NIGHT_ANY
static_assert(value(UiModeNight::No) == 1);   // UI_MODE_NIGHT_NO
static_assert(value(UiModeNight::Yes) == 2);  // UI_MODE_NIGHT_YES

static_assert(value(Touchscreen::Any) == 0);      // TOUCHSCREEN_ANY
static_assert(value(Touchscreen::NoTouch) == 1);  // TOUCHSCREEN_NOTOUCH
static_assert(value(Touchscreen::Stylus) == 2);   // TOUCHSCREEN_STYLUS
static_assert(value(Touchscreen::Finger) == 3);   // TOUCHSCREEN_FINGER

static_assert(value(KeysHidden::Any) == 0);   // KEYSHIDDEN_ANY
static_assert(value(KeysHidden::No) == 1);    // KEYSHIDDEN_NO
static_assert(value(KeysHidden::Yes) == 2);   // KEYSHIDDEN_YES
static_assert(value(KeysHidden::Soft) == 3);  // KEYSHIDDEN_SOFT

static_assert(value(Keyboard::Any) == 0);        // KEYBOARD_ANY
static_assert(value(Keyboard::NoKeys) == 1);     // KEYBOARD_NOKEYS
static_assert(value(Keyboard::Qwerty) == 2);     // KEYBOARD_QWERTY
static_assert(value(Keyboard::TwelveKey) == 3);  // KEYBOARD_12KEY

static_assert(value(NavHidden::Any) == 0);  // NAVHIDDEN_ANY
static_assert(value(NavHidden::No) == 1);   // NAVHIDDEN_NO
static_assert(value(NavHidden::Yes) == 2);  // NAVHIDDEN_YES

static_assert(value(Navigation::Any) == 0);        // NAVIGATION_ANY
static_assert(value(Navigation::NoNav) == 1);      // NAVIGATION_NONAV
static_assert(value(Navigation::Dpad) == 2);       // NAVIGATION_DPAD
static_assert(value(Navigation::Trackball) == 3);  // NAVIGATION_TRACKBALL
static_assert(value(Navigation::Wheel) == 4);      // NAVIGATION_WHEEL

static_assert(kDensityLdpi == 120);     // DENSITY_LOW
static_assert(kDensityMdpi == 160);     // DENSITY_MEDIUM
static_assert(kDensityTvdpi == 213);    // DENSITY_TV
static_assert(kDensityHdpi == 240);     // DENSITY_HIGH
static_assert(kDensityXhdpi == 320);    // DENSITY_XHIGH
static_assert(kDensityXxhdpi == 480);   // DENSITY_XXHIGH
static_assert(kDensityXxxhdpi == 640);  // DENSITY_XXXHIGH
static_assert(kDensityAny == 0xfffe);   // DENSITY_ANY
static_assert(kDensityNone == 0xffff);  // DENSITY_NONE
static_assert(kMncZero == 0xffff);      // MNC_ZERO

// Every field of Config, which operator== compares.
auto fields(const Config& c) {
  return std::tie(c.mcc, c.mnc, c.lang, c.region, c.script, c.variant, c.grammaticalGender,
                  c.layoutDir, c.smallestWidthDp, c.widthDp, c.heightDp, c.screenSize, c.screenLong,
                  c.screenRound, c.wideColorGamut, c.hdr, c.orientation, c.uiModeType,
                  c.uiModeNight, c.density, c.touchscreen, c.keysHidden, c.keyboard, c.navHidden,
                  c.navigation, c.screenWidth, c.screenHeight, c.sdkVersion, c.minorVersion);
}

// A field added to Config and not to fields() would be one operator== does not see (#52). write()
// lists each field by hand too: Config.EveryFieldCountsForEquality fails on one that prints as the
// default does.
static_assert(std::tuple_size_v<decltype(fields(Config{}))> == detail::fieldCount<Config>(),
              "every field of Config must be in fields()");

// The characters of a NUL-padded field.
template <size_t N>
std::string chars(const std::array<char, N>& field) {
  size_t n = 0;
  while (n < N && field[n] != '\0') n++;
  return std::string(field.data(), n);
}

// ResTable_config::appendDirLocale.
void appendLocale(std::string& out, const Config& c) {
  if (c.lang[0] == '\0') return;
  if (c.script[0] == '\0' && c.variant[0] == '\0') {
    out += chars(c.lang);
    if (c.region[0] != '\0') out += "-r" + chars(c.region);
    return;
  }
  out += "b+" + chars(c.lang);
  if (c.script[0] != '\0') out += "+" + chars(c.script);
  if (c.region[0] != '\0') out += "+" + chars(c.region);
  if (c.variant[0] != '\0') out += "+" + chars(c.variant);
}

const char* densityName(uint16_t density) {
  switch (density) {
    case kDensityLdpi: return "ldpi";
    case kDensityMdpi: return "mdpi";
    case kDensityTvdpi: return "tvdpi";
    case kDensityHdpi: return "hdpi";
    case kDensityXhdpi: return "xhdpi";
    case kDensityXxhdpi: return "xxhdpi";
    case kDensityXxxhdpi: return "xxxhdpi";
    case kDensityNone: return "nodpi";
    case kDensityAny: return "anydpi";
    default: return nullptr;
  }
}

// The folder name of `value` in `names` (QualifierNames.h); "" for values without one.
template <typename E, size_t N>
std::string_view nameOf(E value, const std::string_view (&names)[N]) {
  const auto i = static_cast<size_t>(value);
  return i < N ? names[i] : std::string_view();
}

// Every byte of a field up to its last that is not NUL, \xNN for those that are not printable.
template <size_t N>
std::string bytes(const std::array<char, N>& field) {
  size_t n = N;
  while (n > 0 && field[n - 1] == '\0') n--;
  std::string out;
  for (size_t i = 0; i < n; i++) {
    const auto b = static_cast<unsigned char>(field[i]);
    if (b > 0x20 && b < 0x7f) {
      out += field[i];
    } else {
      static constexpr char kHex[] = "0123456789abcdef";
      out += {'\\', 'x', kHex[b >> 4], kHex[b & 0xf]};
    }
  }
  return out;
}

// Whether `locale`, as appendLocale wrote it, reads back as the locale fields of `c`. The b+ form
// does not say which subtag is which, and a field may hold bytes after its first NUL, so one that
// parseConfig would not have made may not.
bool readsBack(const std::string& locale, const Config& c) {
  const std::optional<Config> read = parseConfig(locale);
  return read && read->lang == c.lang && read->region == c.region && read->script == c.script &&
         read->variant == c.variant;
}

// ResTable_config::toString, which writes only what has a folder name. With `all`, also what has
// none, as <field>=<value>: an enum value without a name (UiModeType::Normal, or one out of
// range), and the locale field by field when its folder form would not read back as it, as for a
// region without a language. Two Configs that differ then never come out the same (#50).
std::string write(const Config& c, bool all) {
  std::string out;
  auto part = [&out](std::string_view s) {
    if (s.empty()) return;
    if (!out.empty()) out += '-';
    out += s;
  };
  auto named = [&](std::string_view field, auto value, const auto& names) {
    const std::string_view name = nameOf(value, names);
    if (!name.empty() || !all || value == decltype(value){}) {
      part(name);
    } else {
      part(std::string(field) + "=" + std::to_string(static_cast<int>(value)));
    }
  };
  if (c.mcc != 0) part("mcc" + std::to_string(c.mcc));
  if (c.mnc != 0) part("mnc" + std::to_string(c.mnc));
  std::string locale;
  appendLocale(locale, c);
  const bool anyLocale = !bytes(c.lang).empty() || !bytes(c.region).empty() ||
                         !bytes(c.script).empty() || !bytes(c.variant).empty();
  if (all && anyLocale && !readsBack(locale, c)) {
    if (const std::string b = bytes(c.lang); !b.empty()) part("lang=" + b);
    if (const std::string b = bytes(c.region); !b.empty()) part("region=" + b);
    if (const std::string b = bytes(c.script); !b.empty()) part("script=" + b);
    if (const std::string b = bytes(c.variant); !b.empty()) part("variant=" + b);
  } else {
    part(locale);
  }
  named("grammaticalGender", c.grammaticalGender, names::kGrammaticalGender);
  named("layoutDir", c.layoutDir, names::kLayoutDir);
  if (c.smallestWidthDp != 0) part("sw" + std::to_string(c.smallestWidthDp) + "dp");
  if (c.widthDp != 0) part("w" + std::to_string(c.widthDp) + "dp");
  if (c.heightDp != 0) part("h" + std::to_string(c.heightDp) + "dp");
  named("screenSize", c.screenSize, names::kScreenSize);
  named("screenLong", c.screenLong, names::kScreenLong);
  named("screenRound", c.screenRound, names::kScreenRound);
  named("wideColorGamut", c.wideColorGamut, names::kWideColorGamut);
  named("hdr", c.hdr, names::kHdr);
  named("orientation", c.orientation, names::kOrientation);
  named("uiModeType", c.uiModeType, names::kUiModeType);
  named("uiModeNight", c.uiModeNight, names::kUiModeNight);
  if (c.density != 0) {
    const char* name = densityName(c.density);
    part(name ? name : std::to_string(c.density) + "dpi");
  }
  named("touchscreen", c.touchscreen, names::kTouchscreen);
  named("keysHidden", c.keysHidden, names::kKeysHidden);
  named("keyboard", c.keyboard, names::kKeyboard);
  named("navHidden", c.navHidden, names::kNavHidden);
  named("navigation", c.navigation, names::kNavigation);
  if (c.screenWidth != 0 || c.screenHeight != 0) {
    part(std::to_string(c.screenWidth) + "x" + std::to_string(c.screenHeight));
  }
  if (c.sdkVersion != 0 || c.minorVersion != 0) {
    std::string version = "v" + std::to_string(c.sdkVersion);
    if (c.minorVersion != 0) version += "." + std::to_string(c.minorVersion);
    part(version);
  }
  return out;
}

}  // namespace

bool operator==(const Config& a, const Config& b) { return fields(a) == fields(b); }

std::string toString(const Config& c) { return write(c, /*all=*/false); }

std::ostream& operator<<(std::ostream& os, const Config& config) {
  return os << "Config(" << write(config, /*all=*/true) << ')';
}

}  // namespace hydroid::res

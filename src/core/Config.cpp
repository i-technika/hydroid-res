#include "hydroid/res/Config.h"

#include <ostream>
#include <string>
#include <string_view>
#include <tuple>

#include "core/QualifierNames.h"

namespace hydroid::res {
namespace {

// Every value of the Config enums, and the density constants and kMncZero, is the one Android's
// AConfiguration gives it (#53), so an adapter or a binary table can copy them. The numbers and
// names are those of the NDK's android/configuration.h, r28c (28.2.13676358): the comment after
// each is the constant's name after ACONFIGURATION_.
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

auto fields(const Config& c) {
  return std::tie(c.mcc, c.mnc, c.lang, c.region, c.script, c.variant, c.grammaticalGender,
                  c.layoutDir, c.smallestWidthDp, c.widthDp, c.heightDp, c.screenSize, c.screenLong,
                  c.screenRound, c.wideColorGamut, c.hdr, c.orientation, c.uiModeType,
                  c.uiModeNight, c.density, c.touchscreen, c.keysHidden, c.keyboard, c.navHidden,
                  c.navigation, c.screenWidth, c.screenHeight, c.sdkVersion, c.minorVersion);
}

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

}  // namespace

bool operator==(const Config& a, const Config& b) { return fields(a) == fields(b); }

// ResTable_config::toString.
std::string toString(const Config& c) {
  std::string out;
  auto part = [&out](std::string_view s) {
    if (s.empty()) return;
    if (!out.empty()) out += '-';
    out += s;
  };
  if (c.mcc != 0) part("mcc" + std::to_string(c.mcc));
  if (c.mnc != 0) part("mnc" + std::to_string(c.mnc));
  std::string locale;
  appendLocale(locale, c);
  part(locale);
  part(nameOf(c.grammaticalGender, names::kGrammaticalGender));
  part(nameOf(c.layoutDir, names::kLayoutDir));
  if (c.smallestWidthDp != 0) part("sw" + std::to_string(c.smallestWidthDp) + "dp");
  if (c.widthDp != 0) part("w" + std::to_string(c.widthDp) + "dp");
  if (c.heightDp != 0) part("h" + std::to_string(c.heightDp) + "dp");
  part(nameOf(c.screenSize, names::kScreenSize));
  part(nameOf(c.screenLong, names::kScreenLong));
  part(nameOf(c.screenRound, names::kScreenRound));
  part(nameOf(c.wideColorGamut, names::kWideColorGamut));
  part(nameOf(c.hdr, names::kHdr));
  part(nameOf(c.orientation, names::kOrientation));
  part(nameOf(c.uiModeType, names::kUiModeType));
  part(nameOf(c.uiModeNight, names::kUiModeNight));
  if (c.density != 0) {
    const char* name = densityName(c.density);
    part(name ? name : std::to_string(c.density) + "dpi");
  }
  part(nameOf(c.touchscreen, names::kTouchscreen));
  part(nameOf(c.keysHidden, names::kKeysHidden));
  part(nameOf(c.keyboard, names::kKeyboard));
  part(nameOf(c.navHidden, names::kNavHidden));
  part(nameOf(c.navigation, names::kNavigation));
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

std::ostream& operator<<(std::ostream& os, const Config& config) {
  return os << "Config(" << toString(config) << ')';
}

}  // namespace hydroid::res

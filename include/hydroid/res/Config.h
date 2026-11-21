#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>

#include "hydroid/res/Export.h"

namespace hydroid::res {

// The qualifiers of one resource folder, or the state of the device. Every qualifier takes the
// values of Android's AConfiguration (and ResTable_config), and 0 is "any": not given in a folder
// name, or not known on a device. A value added here needs its static_assert in Config.cpp.
//
// parseConfig (ConfigParser.h) makes one from a folder name, and toString prints it back as aapt2
// does. matches and isBetterThan below say which resources a device takes, and Resources
// (Resources.h) picks them from a table.
//
// A device's Config gives its locale as Android gives one to its resources: the language as its
// old code where it has one (iw, in, ji, not he, id, yi), and the script only where the tag
// names one ("sr-Latn"); matching works out the script of the others. Its sdkVersion is
// kEmulatedSdkVersion (Version.h), its keysHidden is KeysHidden::Soft where the keyboard is the
// soft one, and its navHidden is NavHidden::Any, as Android tells its resources nothing of it:
// values-navexposed/ and values-navhidden/ are of no Android device.
// Resources::setConfiguration sees to the language and the version.
//
// The numbering system of a device's locale (the nu-latn of ar-EG-u-nu-latn), which
// ResTable_config keeps, has no field here (#51). A folder cannot name one, as aapt2 link
// rejects b+ar+u+nu+latn, so no resource has any, and Android only uses the device's to tell
// two resources apart by theirs.

enum class GrammaticalGender : uint8_t { Any = 0, Neuter = 1, Feminine = 2, Masculine = 3 };
enum class LayoutDir : uint8_t { Any = 0, Ltr = 1, Rtl = 2 };
enum class ScreenSize : uint8_t { Any = 0, Small = 1, Normal = 2, Large = 3, XLarge = 4 };
enum class ScreenLong : uint8_t { Any = 0, No = 1, Yes = 2 };      // notlong, long
enum class ScreenRound : uint8_t { Any = 0, No = 1, Yes = 2 };     // notround, round
enum class WideColorGamut : uint8_t { Any = 0, No = 1, Yes = 2 };  // nowidecg, widecg
enum class Hdr : uint8_t { Any = 0, No = 1, Yes = 2 };             // lowdr, highdr
enum class Orientation : uint8_t { Any = 0, Port = 1, Land = 2, Square = 3 };
// Normal has no qualifier: it is the type of a device that is none of the others.
// clang-format off
enum class UiModeType : uint8_t {
  Any = 0, Normal = 1, Desk = 2, Car = 3, Television = 4, Appliance = 5, Watch = 6, VrHeadset = 7
};
// clang-format on
enum class UiModeNight : uint8_t { Any = 0, No = 1, Yes = 2 };  // notnight, night
enum class Touchscreen : uint8_t { Any = 0, NoTouch = 1, Stylus = 2, Finger = 3 };
enum class KeysHidden : uint8_t { Any = 0, No = 1, Yes = 2, Soft = 3 };  // keysexposed, ...
enum class Keyboard : uint8_t { Any = 0, NoKeys = 1, Qwerty = 2, TwelveKey = 3 };
enum class NavHidden : uint8_t { Any = 0, No = 1, Yes = 2 };  // navexposed, navhidden
enum class Navigation : uint8_t { Any = 0, NoNav = 1, Dpad = 2, Trackball = 3, Wheel = 4 };

// Config::density. Any other value is that many dots per inch ("400dpi").
inline constexpr uint16_t kDensityLdpi = 120;
inline constexpr uint16_t kDensityMdpi = 160;
inline constexpr uint16_t kDensityTvdpi = 213;
inline constexpr uint16_t kDensityHdpi = 240;
inline constexpr uint16_t kDensityXhdpi = 320;
inline constexpr uint16_t kDensityXxhdpi = 480;
inline constexpr uint16_t kDensityXxxhdpi = 640;
inline constexpr uint16_t kDensityAny = 0xfffe;   // anydpi
inline constexpr uint16_t kDensityNone = 0xffff;  // nodpi

// Config::mnc of mnc0, mnc00 and mnc000, as 0 is "any".
inline constexpr uint16_t kMncZero = 0xffff;

// The fields are in the order of the qualifiers in a folder name, which is also their precedence.
// A field added here goes in fields() and write() in Config.cpp too, and in the test
// Config.EveryFieldCountsForEquality; the build fails without the first and the last.
struct Config {
  uint16_t mcc = 0;  // mcc310: 310
  uint16_t mnc = 0;  // mnc260: 260

  // The locale, NUL-padded. A folder gives them as ko-rKR, or in BCP 47 as b+sr+Latn+RS.
  std::array<char, 4> lang{};     // 2 or 3 lowercase letters: "ko", "fil"
  std::array<char, 4> region{};   // 2 uppercase letters or digits, or 3 digits: "KR", "419"
  std::array<char, 4> script{};   // 4 letters or none, as written: "Latn". Not NUL-terminated.
  std::array<char, 8> variant{};  // up to 8 lowercase letters or digits: "posix", "1901"

  GrammaticalGender grammaticalGender = GrammaticalGender::Any;
  LayoutDir layoutDir = LayoutDir::Any;
  uint16_t smallestWidthDp = 0;  // sw600dp: 600
  uint16_t widthDp = 0;          // w720dp
  uint16_t heightDp = 0;         // h480dp
  ScreenSize screenSize = ScreenSize::Any;
  ScreenLong screenLong = ScreenLong::Any;
  ScreenRound screenRound = ScreenRound::Any;
  WideColorGamut wideColorGamut = WideColorGamut::Any;
  Hdr hdr = Hdr::Any;
  Orientation orientation = Orientation::Any;
  UiModeType uiModeType = UiModeType::Any;
  UiModeNight uiModeNight = UiModeNight::Any;
  uint16_t density = 0;  // dpi, or kDensityAny, kDensityNone
  Touchscreen touchscreen = Touchscreen::Any;
  KeysHidden keysHidden = KeysHidden::Any;
  Keyboard keyboard = Keyboard::Any;
  NavHidden navHidden = NavHidden::Any;
  Navigation navigation = Navigation::Any;
  // The screen in pixels, 640x480. Deprecated since Android 3.2, but aapt2 still reads it.
  uint16_t screenWidth = 0;
  uint16_t screenHeight = 0;
  uint16_t sdkVersion = 0;    // v21: 21
  uint16_t minorVersion = 0;  // v36.1: 1. From Android 16 (v36) on.
};

HYDROID_RES_API bool operator==(const Config& a, const Config& b);
inline bool operator!=(const Config& a, const Config& b) { return !(a == b); }

// < 0 if `a` comes before `b`, 0 if they are equal, > 0 if after: the order of
// ResTable_config::compare, in which aapt2 keeps the values of a resource and
// `aapt2 dump resources` lists them. It compares Android's packed fields as integers, so it is
// not the order of the qualifiers in a folder name: the mnc before the mcc, the region before the
// language and the second letter of each before the first, and the density before the
// touchscreen before the orientation. The version comes before the minor version: v36, v36.1, v37.
HYDROID_RES_API int compareConfigs(const Config& a, const Config& b);

// The qualifiers as aapt2 writes them, which is how `aapt2 dump resources` prints a configuration:
// "en-rUS-sw600dp-v13", "b+sr+Latn", "" for the default Config. Every field with a folder name
// is written, in folder order, under that name: mnc as a number (kMncZero is "mnc65535"), a
// density without a name as "400dpi", and a locale with a script or variant in the b+ form. A
// locale with a 3-digit region and neither, such as b+es+419, is written "es-r419", as aapt2
// does, although no folder can be named so. What no folder names is left out: UiModeType::Normal,
// an enum value out of range, and a region, script or variant without a language. Two Configs
// that differ only there come out the same; operator<< tells them apart.
HYDROID_RES_API std::string toString(const Config& config);

// For logs and test failures: Config(en-rUS-v13), Config() for the default. Unlike toString, it
// also writes what has no folder name, as <field>=<value>: Config(uiModeType=1-420dpi),
// Config(orientation=9). A locale whose folder form would not read back as these very fields goes
// field by field, every byte: Config(region=KR), Config(lang=sr-variant=Latn). Two Configs that
// differ never print the same (#50).
HYDROID_RES_API std::ostream& operator<<(std::ostream& os, const Config& config);

// Whether a resource of the configuration `config` can be used on `device`, as
// ResTable_config::match says: every qualifier `config` gives must agree with the device's.
//   - The same as the device's: mcc, mnc, grammatical gender, layout direction, long, round,
//     wide color gamut, HDR, orientation, UI mode type, night mode, touchscreen, keyboard,
//     navigation and their hidden states, but that keysexposed also matches keyssoft.
//   - No more than the device's: smallest width, width and height in dp, screen size, the
//     screen in pixels, and the version, with its minor version.
//   - The density always matches: Android scales what it finds.
//   - The locale: the same language (tl and fil are one), then the same script, each locale's
//     being the one it names or else the one its language is most likely written in there
//     (zh-rTW is Hant, so it matches a zh-HK device and zh-rCN does not; en-rGB matches en-AU).
//     The region and the variant keep nothing out. Only where a script is not known, as of a
//     private-use language, must a region the resource gives be the device's.
HYDROID_RES_API bool matches(const Config& config, const Config& device);

// Whether `a` is a better configuration than `b` for `device`, both of which match it, as
// ResTable_config::isBetterThan says. The qualifiers are compared in the order of Config's
// fields, but that both hidden states come before the keyboard and the navigation, and the first
// that differs decides, where the device gives it:
//   - Most: the configuration that gives the qualifier is better than the one that does not.
//   - Smallest width, version: the larger. Width and height in dp, the screen in pixels: the
//     closer to the device's, both together.
//   - Screen size: the larger, none counting as normal on a screen that is at least normal.
//   - Density, where none is mdpi: anydpi first; otherwise the higher, to scale down, unless
//     both are at least the device's, and then the lower.
//   - Locale: one with the language over one without, but for an en-US device, and one whose
//     English is close to it, where no language is better than an English of another kind
//     (en-rGB); then the region closer to the device's among the parents of its locale (es-r419
//     for es-MX), then the same variant.
// It is not an order: a better locale decides at once, but a worse one does not, so two
// configurations can each be better than the other (ko and sw600dp, on a Korean tablet). What
// Android picks then depends on the order it meets them in, which bestCandidate (Resources.h)
// follows.
HYDROID_RES_API bool isBetterThan(const Config& a, const Config& b, const Config& device);

}  // namespace hydroid::res

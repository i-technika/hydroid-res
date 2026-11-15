#pragma once

#include <cstddef>
#include <string_view>

#include "hydroid/res/Config.h"

// The folder name of each value of the Config enums, by value, which parseConfig reads and
// toString writes (#55). "" is a value without one: Any, and UiModeType::Normal, the type of a
// device that is none of the others ("normal" is a screen size).

namespace hydroid::res::names {

inline constexpr std::string_view kGrammaticalGender[] = {"", "neuter", "feminine", "masculine"};
inline constexpr std::string_view kLayoutDir[] = {"", "ldltr", "ldrtl"};
inline constexpr std::string_view kScreenSize[] = {"", "small", "normal", "large", "xlarge"};
inline constexpr std::string_view kScreenLong[] = {"", "notlong", "long"};
inline constexpr std::string_view kScreenRound[] = {"", "notround", "round"};
inline constexpr std::string_view kWideColorGamut[] = {"", "nowidecg", "widecg"};
inline constexpr std::string_view kHdr[] = {"", "lowdr", "highdr"};
inline constexpr std::string_view kOrientation[] = {"", "port", "land", "square"};
inline constexpr std::string_view kUiModeType[] = {"",           "",          "desk",  "car",
                                                   "television", "appliance", "watch", "vrheadset"};
inline constexpr std::string_view kUiModeNight[] = {"", "notnight", "night"};
inline constexpr std::string_view kTouchscreen[] = {"", "notouch", "stylus", "finger"};
inline constexpr std::string_view kKeysHidden[] = {"", "keysexposed", "keyshidden", "keyssoft"};
inline constexpr std::string_view kKeyboard[] = {"", "nokeys", "qwerty", "12key"};
inline constexpr std::string_view kNavHidden[] = {"", "navexposed", "navhidden"};
inline constexpr std::string_view kNavigation[] = {"", "nonav", "dpad", "trackball", "wheel"};

// Each table has a name, or "", for every value up to the enum's last.
template <typename E, size_t N>
constexpr bool endsAt(const std::string_view (&)[N], E last) {
  return N == static_cast<size_t>(last) + 1;
}
static_assert(endsAt(kGrammaticalGender, GrammaticalGender::Masculine));
static_assert(endsAt(kLayoutDir, LayoutDir::Rtl));
static_assert(endsAt(kScreenSize, ScreenSize::XLarge));
static_assert(endsAt(kScreenLong, ScreenLong::Yes));
static_assert(endsAt(kScreenRound, ScreenRound::Yes));
static_assert(endsAt(kWideColorGamut, WideColorGamut::Yes));
static_assert(endsAt(kHdr, Hdr::Yes));
static_assert(endsAt(kOrientation, Orientation::Square));
static_assert(endsAt(kUiModeType, UiModeType::VrHeadset));
static_assert(endsAt(kUiModeNight, UiModeNight::Yes));
static_assert(endsAt(kTouchscreen, Touchscreen::Finger));
static_assert(endsAt(kKeysHidden, KeysHidden::Soft));
static_assert(endsAt(kKeyboard, Keyboard::TwelveKey));
static_assert(endsAt(kNavHidden, NavHidden::Yes));
static_assert(endsAt(kNavigation, Navigation::Wheel));

}  // namespace hydroid::res::names

#include "hydroid/res/Config.h"

#include <ostream>
#include <string>
#include <string_view>
#include <tuple>

#include "core/QualifierNames.h"

namespace hydroid::res {
namespace {

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

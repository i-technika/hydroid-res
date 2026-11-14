#include "hydroid/res/Config.h"

#include <ostream>
#include <string>
#include <tuple>

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

// The folder name of each enumerator, by its value; "" for Any and for values without one.
template <typename E, size_t N>
const char* nameOf(E value, const char* const (&names)[N]) {
  const auto i = static_cast<size_t>(value);
  return i < N ? names[i] : "";
}

}  // namespace

bool operator==(const Config& a, const Config& b) { return fields(a) == fields(b); }

// ResTable_config::toString.
std::string toString(const Config& c) {
  std::string out;
  auto part = [&out](const std::string& s) {
    if (s.empty()) return;
    if (!out.empty()) out += '-';
    out += s;
  };
  if (c.mcc != 0) part("mcc" + std::to_string(c.mcc));
  if (c.mnc != 0) part("mnc" + std::to_string(c.mnc));
  std::string locale;
  appendLocale(locale, c);
  part(locale);
  part(nameOf(c.grammaticalGender, {"", "neuter", "feminine", "masculine"}));
  part(nameOf(c.layoutDir, {"", "ldltr", "ldrtl"}));
  if (c.smallestWidthDp != 0) part("sw" + std::to_string(c.smallestWidthDp) + "dp");
  if (c.widthDp != 0) part("w" + std::to_string(c.widthDp) + "dp");
  if (c.heightDp != 0) part("h" + std::to_string(c.heightDp) + "dp");
  part(nameOf(c.screenSize, {"", "small", "normal", "large", "xlarge"}));
  part(nameOf(c.screenLong, {"", "notlong", "long"}));
  part(nameOf(c.screenRound, {"", "notround", "round"}));
  part(nameOf(c.wideColorGamut, {"", "nowidecg", "widecg"}));
  part(nameOf(c.hdr, {"", "lowdr", "highdr"}));
  part(nameOf(c.orientation, {"", "port", "land", "square"}));
  part(nameOf(c.uiModeType,
              {"", "", "desk", "car", "television", "appliance", "watch", "vrheadset"}));
  part(nameOf(c.uiModeNight, {"", "notnight", "night"}));
  if (c.density != 0) {
    const char* name = densityName(c.density);
    part(name ? name : std::to_string(c.density) + "dpi");
  }
  part(nameOf(c.touchscreen, {"", "notouch", "stylus", "finger"}));
  part(nameOf(c.keysHidden, {"", "keysexposed", "keyshidden", "keyssoft"}));
  part(nameOf(c.keyboard, {"", "nokeys", "qwerty", "12key"}));
  part(nameOf(c.navHidden, {"", "navexposed", "navhidden"}));
  part(nameOf(c.navigation, {"", "nonav", "dpad", "trackball", "wheel"}));
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

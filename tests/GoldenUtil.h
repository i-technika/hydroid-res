// Walking golden/data, and the device of each golden condition, shared by the golden tests.

#pragma once

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/ConfigParser.h"

namespace hydroid::res::test {

// Calls fn(condition, json) for each golden/data/*.json. The condition is the file stem,
// "160dpi_en-US_notnight_port_sw320dp".
template <typename Fn>
void forEachGoldenCondition(Fn&& fn) {
  for (const auto& file : std::filesystem::directory_iterator(HYDROID_GOLDEN_DIR)) {
    if (file.path().extension() != ".json") continue;
    std::ifstream in(file.path());
    fn(file.path().stem().string(), nlohmann::json::parse(in));
  }
}

// The device of a golden condition: its Config and the locales that were asked for, to give
// Resources::setConfiguration.
struct GoldenDevice {
  Config config;
  std::vector<Config> locales;
};

// What Android makes of the android.content.res.Configuration a condition records, on its way to
// the resource table: ResourcesImpl.updateConfiguration, then NativeSetConfiguration
// (frameworks/base core/jni/android_util_AssetManager.cpp), which cuts the Java fields into
// ResTable_config's.
inline GoldenDevice goldenDevice(const nlohmann::json& condition) {
  const nlohmann::json& c = condition.at("configuration");
  const auto at = [&](const char* key) { return c.at(key).get<int>(); };
  GoldenDevice device;
  Config& config = device.config;
  config.mcc = static_cast<uint16_t>(at("mcc"));
  config.mnc = static_cast<uint16_t>(at("mnc"));
  config.grammaticalGender = static_cast<GrammaticalGender>(at("grammaticalGender"));
  // Configuration.screenLayout: the size, long, the direction, and round in the next byte.
  const int screenLayout = at("screenLayout");
  config.screenSize = static_cast<ScreenSize>(screenLayout & 0x0f);
  config.screenLong = static_cast<ScreenLong>((screenLayout & 0x30) >> 4);
  config.layoutDir = static_cast<LayoutDir>((screenLayout & 0xc0) >> 6);
  config.screenRound = static_cast<ScreenRound>((screenLayout & 0x300) >> 8);
  config.smallestWidthDp = static_cast<uint16_t>(at("smallestScreenWidthDp"));
  config.widthDp = static_cast<uint16_t>(at("screenWidthDp"));
  config.heightDp = static_cast<uint16_t>(at("screenHeightDp"));
  const int colorMode = at("colorMode");
  config.wideColorGamut = static_cast<WideColorGamut>(colorMode & 0x3);
  config.hdr = static_cast<Hdr>((colorMode & 0xc) >> 2);
  config.orientation = static_cast<Orientation>(at("orientation"));
  const int uiMode = at("uiMode");
  config.uiModeType = static_cast<UiModeType>(uiMode & 0x0f);
  config.uiModeNight = static_cast<UiModeNight>((uiMode & 0x30) >> 4);
  config.density = static_cast<uint16_t>(at("densityDpi"));
  config.touchscreen = static_cast<Touchscreen>(at("touchscreen"));
  // KEYBOARDHIDDEN_NO with HARDKEYBOARDHIDDEN_YES is the soft keyboard.
  config.keysHidden = at("keyboardHidden") == 1 && at("hardKeyboardHidden") == 2
                          ? KeysHidden::Soft
                          : static_cast<KeysHidden>(at("keyboardHidden"));
  config.keyboard = static_cast<Keyboard>(at("keyboard"));
  // Android gives its resources no navigationHidden: navHidden stays Any.
  config.navigation = static_cast<Navigation>(at("navigation"));
  // The display in pixels, its longer side as the width.
  config.screenWidth = static_cast<uint16_t>(std::max(at("widthPixels"), at("heightPixels")));
  config.screenHeight = static_cast<uint16_t>(std::min(at("widthPixels"), at("heightPixels")));
  const int sdkIntFull = condition.at("sdkIntFull").get<int>();
  config.sdkVersion = static_cast<uint16_t>(sdkIntFull / 100000);
  config.minorVersion = static_cast<uint16_t>(sdkIntFull % 100000);

  const std::string tags = condition.at("requestedLocales").get<std::string>();
  for (size_t start = 0; start <= tags.size();) {
    const size_t comma = std::min(tags.find(',', start), tags.size());
    const std::optional<Config> locale = parseLanguageTag(tags.substr(start, comma - start));
    EXPECT_TRUE(locale) << tags;
    if (locale) device.locales.push_back(*locale);
    start = comma + 1;
  }
  return device;
}

}  // namespace hydroid::res::test

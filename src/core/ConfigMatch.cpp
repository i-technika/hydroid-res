#include <algorithm>
#include <array>
#include <cstring>

#include "core/LocaleData.h"
#include "core/LocalePack.h"
#include "hydroid/res/Config.h"

// A port of ResTable_config::match, isBetterThan and isLocaleBetterThan (frameworks/base
// libs/androidfw/ResourceTypes.cpp, android17-release), field for field and in their order, which
// is the precedence of the qualifiers.

namespace hydroid::res {
namespace {

using detail::LocaleCode;
using detail::ScriptCode;

// The locale of a Config as ResTable_config holds one.
struct Locale {
  LocaleCode language;
  LocaleCode country;
  ScriptCode script;
  std::array<char, 8> variant;

  bool any() const { return language == LocaleCode{} && country == LocaleCode{}; }
};

Locale localeOf(const Config& c) {
  return {detail::packLanguage(c.lang), detail::packRegion(c.region), c.script, c.variant};
}

// The locale of the device. Android computes the script of one that names none when it sets the
// configuration (ResTable_config::setBcp47Locale), so a device's script is only missing where
// the tables do not know its language.
Locale requestedLocaleOf(const Config& device) {
  Locale locale = localeOf(device);
  if (locale.script[0] == '\0')
    locale.script = detail::computeScript(locale.language, locale.country);
  return locale;
}

constexpr LocaleCode kEnglish = {'e', 'n'};
constexpr LocaleCode kUnitedStates = {'U', 'S'};
constexpr LocaleCode kFilipino = {'\xAD', '\x05'};  // "fil", packed
constexpr LocaleCode kTagalog = {'t', 'l'};

bool langsAreEquivalent(const LocaleCode& a, const LocaleCode& b) {
  return a == b || (a == kTagalog && b == kFilipino) || (a == kFilipino && b == kTagalog);
}

// strncmp(a, b, 8) == 0: the same up to the first NUL.
bool sameVariant(const std::array<char, 8>& a, const std::array<char, 8>& b) {
  return std::strncmp(a.data(), b.data(), a.size()) == 0;
}

// The locale part of ResTable_config::match.
bool localeMatches(const Locale& locale, const Locale& requested) {
  if (locale.any()) return true;
  // The region and the variant do not keep a locale from matching: isLocaleBetterThan weighs
  // them.
  if (!langsAreEquivalent(locale.language, requested.language)) return false;

  // The scripts must be the same, where both are known. Where one is not, as of a private-use
  // language, it is the regions that must be, as before Android 7.
  bool countriesMustMatch = false;
  ScriptCode script{};
  if (requested.script[0] == '\0') {
    countriesMustMatch = true;
  } else if (locale.script[0] == '\0') {
    script = detail::computeScript(locale.language, locale.country);
    if (script[0] == '\0') countriesMustMatch = true;
  } else {
    script = locale.script;
  }

  if (countriesMustMatch) {
    return locale.country[0] == '\0' || locale.country == requested.country;
  }
  return script == requested.script;
}

// ResTable_config::isLocaleBetterThan, of two locales that match the requested one.
bool isLocaleBetterThan(const Locale& a, const Locale& b, const Locale& requested) {
  if (!langsAreEquivalent(a.language, b.language)) {
    // One has the language and the other none. The one with the language is the better, but for
    // US English and the Englishes close to it, whose resources apps keep without a language:
    // there, the one without is better than an English that descends from en-001.
    if (requested.language == kEnglish) {
      if (requested.country == kUnitedStates) {
        if (a.language[0] != '\0') {
          return a.country[0] == '\0' || a.country == kUnitedStates;
        }
        return !(b.country[0] == '\0' || b.country == kUnitedStates);
      }
      if (detail::isCloseToUsEnglish(requested.country)) {
        if (a.language[0] != '\0') return detail::isCloseToUsEnglish(a.country);
        return !detail::isCloseToUsEnglish(b.country);
      }
    }
    return a.language[0] != '\0';
  }

  // Both have the language, and the script too, as they matched: the region decides, then the
  // variant.
  const int regions = detail::compareRegions(a.country, b.country, requested.language,
                                             requested.script, requested.country);
  if (regions != 0) return regions > 0;

  const bool variantMatches = sameVariant(a.variant, requested.variant);
  const bool otherVariantMatches = sameVariant(b.variant, requested.variant);
  if (variantMatches != otherVariantMatches) return variantMatches;

  // Android compares the numbering systems here. A folder cannot name one, so the two are
  // always the same (#51).

  // Tagalog and Filipino are equivalent, but the very language asked for is the better.
  return a.language == requested.language && b.language != requested.language;
}

template <typename E>
int n(E e) {
  return static_cast<int>(e);
}

}  // namespace

bool matches(const Config& c, const Config& device) {
  if (c.mcc != 0 && c.mcc != device.mcc) return false;
  if (c.mnc != 0 && c.mnc != device.mnc) return false;

  if (const Locale locale = localeOf(c); !locale.any()) {
    if (!localeMatches(locale, requestedLocaleOf(device))) return false;
  }

  if (n(c.grammaticalGender) != 0 && c.grammaticalGender != device.grammaticalGender) return false;

  if (n(c.layoutDir) != 0 && c.layoutDir != device.layoutDir) return false;
  // A screen size fits the screens of its size and larger.
  if (n(c.screenSize) != 0 && n(c.screenSize) > n(device.screenSize)) return false;
  if (n(c.screenLong) != 0 && c.screenLong != device.screenLong) return false;
  if (n(c.uiModeType) != 0 && c.uiModeType != device.uiModeType) return false;
  if (n(c.uiModeNight) != 0 && c.uiModeNight != device.uiModeNight) return false;
  if (c.smallestWidthDp != 0 && c.smallestWidthDp > device.smallestWidthDp) return false;

  if (n(c.screenRound) != 0 && c.screenRound != device.screenRound) return false;
  if (n(c.hdr) != 0 && c.hdr != device.hdr) return false;
  if (n(c.wideColorGamut) != 0 && c.wideColorGamut != device.wideColorGamut) return false;

  if (c.widthDp != 0 && c.widthDp > device.widthDp) return false;
  if (c.heightDp != 0 && c.heightDp > device.heightDp) return false;

  if (n(c.orientation) != 0 && c.orientation != device.orientation) return false;
  // Any density matches, as Android scales what it finds: isBetterThan picks one.
  if (n(c.touchscreen) != 0 && c.touchscreen != device.touchscreen) return false;

  if (n(c.keysHidden) != 0 && c.keysHidden != device.keysHidden) {
    // keysexposed also matches a device whose keyboard is the soft one.
    if (c.keysHidden != KeysHidden::No || device.keysHidden != KeysHidden::Soft) return false;
  }
  if (n(c.navHidden) != 0 && c.navHidden != device.navHidden) return false;
  if (n(c.keyboard) != 0 && c.keyboard != device.keyboard) return false;
  if (n(c.navigation) != 0 && c.navigation != device.navigation) return false;

  if (c.screenWidth != 0 && c.screenWidth > device.screenWidth) return false;
  if (c.screenHeight != 0 && c.screenHeight > device.screenHeight) return false;

  if (c.sdkVersion != 0 && c.sdkVersion > device.sdkVersion) return false;
  if (c.minorVersion != 0) {
    // A minor version of version 0 is of no version at all.
    if (c.sdkVersion == 0) return false;
    if (c.sdkVersion == device.sdkVersion && c.minorVersion > device.minorVersion) return false;
  }
  return true;
}

bool isBetterThan(const Config& a, const Config& b, const Config& device) {
  if (a.mcc != b.mcc && device.mcc != 0) return a.mcc != 0;
  if (a.mnc != b.mnc && device.mnc != 0) return a.mnc != 0;

  // Only a better locale ends the comparison here: with a worse one it goes on, as in Android,
  // so that each of two configurations can be better than the other, one by its locale and the
  // other by a later qualifier. The first of them a resource has then stays (bestCandidate).
  if (!localeOf(device).any()) {
    const Locale localeA = localeOf(a);
    const Locale localeB = localeOf(b);
    if ((!localeA.any() || !localeB.any()) &&
        isLocaleBetterThan(localeA, localeB, requestedLocaleOf(device))) {
      return true;
    }
  }

  if (a.grammaticalGender != b.grammaticalGender && n(device.grammaticalGender) != 0) {
    return n(a.grammaticalGender) != 0;
  }

  if (a.layoutDir != b.layoutDir && n(device.layoutDir) != 0) {
    return n(a.layoutDir) > n(b.layoutDir);
  }

  // The widths and heights that are too large are out already, so the largest is the closest.
  if (a.smallestWidthDp != b.smallestWidthDp) return a.smallestWidthDp > b.smallestWidthDp;

  if (a.widthDp != 0 || a.heightDp != 0 || b.widthDp != 0 || b.heightDp != 0) {
    // By how far the width and the height are from the device's, together. One that is not
    // given is as far as can be, so a configuration that gives it is the better.
    int delta = 0;
    int otherDelta = 0;
    if (device.widthDp != 0) {
      delta += device.widthDp - a.widthDp;
      otherDelta += device.widthDp - b.widthDp;
    }
    if (device.heightDp != 0) {
      delta += device.heightDp - a.heightDp;
      otherDelta += device.heightDp - b.heightDp;
    }
    if (delta != otherDelta) return delta < otherDelta;
  }

  if (a.screenSize != b.screenSize && n(device.screenSize) != 0) {
    // No size counts as normal, on a screen that is at least normal; on a small one, small is
    // better than none.
    const int size = n(a.screenSize);
    const int otherSize = n(b.screenSize);
    int fixedSize = size;
    int fixedOtherSize = otherSize;
    if (n(device.screenSize) >= n(ScreenSize::Normal)) {
      if (fixedSize == 0) fixedSize = n(ScreenSize::Normal);
      if (fixedOtherSize == 0) fixedOtherSize = n(ScreenSize::Normal);
    }
    // The same once fixed: the one that says normal is better than the one that says nothing.
    if (fixedSize == fixedOtherSize) return size != 0;
    return fixedSize > fixedOtherSize;
  }
  if (a.screenLong != b.screenLong && n(device.screenLong) != 0) return n(a.screenLong) != 0;

  if (a.screenRound != b.screenRound && n(device.screenRound) != 0) return n(a.screenRound) != 0;

  if (a.wideColorGamut != b.wideColorGamut && n(device.wideColorGamut) != 0) {
    return n(a.wideColorGamut) != 0;
  }
  if (a.hdr != b.hdr && n(device.hdr) != 0) return n(a.hdr) != 0;

  if (a.orientation != b.orientation && n(device.orientation) != 0) return n(a.orientation) != 0;

  if (a.uiModeType != b.uiModeType && n(device.uiModeType) != 0) return n(a.uiModeType) != 0;
  if (a.uiModeNight != b.uiModeNight && n(device.uiModeNight) != 0) return n(a.uiModeNight) != 0;

  if (a.density != b.density) {
    // No density is mdpi, the density of a resource that names none.
    const int density = a.density != 0 ? a.density : kDensityMdpi;
    const int otherDensity = b.density != 0 ? b.density : kDensityMdpi;

    // anydpi is better than any density, which would be scaled.
    if (density == kDensityAny) return true;
    if (otherDensity == kDensityAny) return false;

    int requested = device.density;
    if (requested == 0 || requested == kDensityAny) requested = kDensityMdpi;

    // Of two densities, the higher, to scale down rather than up, unless both are at or above
    // the device's: then the lower, which is the closer.
    int high = density;
    int low = otherDensity;
    bool iAmBigger = true;
    if (low > high) {
      std::swap(low, high);
      iAmBigger = false;
    }
    if (high == requested) return iAmBigger;
    if (low >= requested) return !iAmBigger;
    return iAmBigger;
  }

  if (a.touchscreen != b.touchscreen && n(device.touchscreen) != 0) return n(a.touchscreen) != 0;

  if (a.keysHidden != b.keysHidden && n(device.keysHidden) != 0) {
    if (n(a.keysHidden) == 0) return false;
    if (n(b.keysHidden) == 0) return true;
    // keysexposed matches keyssoft too, so the one that is the device's own is the better.
    if (device.keysHidden == a.keysHidden) return true;
    if (device.keysHidden == b.keysHidden) return false;
  }
  if (a.navHidden != b.navHidden && n(device.navHidden) != 0) {
    if (n(a.navHidden) == 0) return false;
    if (n(b.navHidden) == 0) return true;
  }
  if (a.keyboard != b.keyboard && n(device.keyboard) != 0) return n(a.keyboard) != 0;
  if (a.navigation != b.navigation && n(device.navigation) != 0) return n(a.navigation) != 0;

  if (a.screenWidth != 0 || a.screenHeight != 0 || b.screenWidth != 0 || b.screenHeight != 0) {
    // As the width and height in dp above.
    int delta = 0;
    int otherDelta = 0;
    if (device.screenWidth != 0) {
      delta += device.screenWidth - a.screenWidth;
      otherDelta += device.screenWidth - b.screenWidth;
    }
    if (device.screenHeight != 0) {
      delta += device.screenHeight - a.screenHeight;
      otherDelta += device.screenHeight - b.screenHeight;
    }
    if (delta != otherDelta) return delta < otherDelta;
  }

  if (device.sdkVersion != 0 || device.minorVersion != 0) {
    if (a.sdkVersion != b.sdkVersion) return a.sdkVersion > b.sdkVersion;
    if (a.minorVersion != b.minorVersion) return a.minorVersion > b.minorVersion;
  }
  return false;
}

}  // namespace hydroid::res

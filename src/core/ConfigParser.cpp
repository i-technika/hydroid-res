#include "hydroid/res/ConfigParser.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/QualifierNames.h"

// A port of aapt2's ConfigDescription::Parse and LocaleValue::InitFromParts (frameworks/base
// libs/androidfw, android17-release), with the deliberate rejects ConfigParser.h lists. Each
// qualifier has a parser that takes one part or none, and they run in folder order.

namespace hydroid::res {
namespace {

using namespace std::string_view_literals;

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
char toLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
char toUpper(char c) { return isLower(c) ? static_cast<char>(c - 'a' + 'A') : c; }

bool allOf(std::string_view s, bool (*pred)(char)) {
  for (char c : s) {
    if (!pred(c)) return false;
  }
  return true;
}
bool isAlnum(char c) { return isDigit(c) || isLower(c); }  // parts are lowercase

// Splits on '-' and lowercases ASCII, as util::SplitAndLowercase. "" is one empty part.
std::vector<std::string> splitLower(std::string_view s, char sep) {
  std::vector<std::string> parts(1);
  for (char c : s) {
    if (c == sep) {
      parts.emplace_back();
    } else {
      parts.back() += toLower(c);
    }
  }
  return parts;
}

// `s` without `prefix`, if it starts with it.
bool consume(std::string_view& s, std::string_view prefix) {
  if (s.substr(0, prefix.size()) != prefix) return false;
  s.remove_prefix(prefix.size());
  return true;
}

// A decimal number of at least one digit that fills `s`, up to 65535. aapt2 wraps the larger
// ones to 16 bits in every qualifier but the version, which rejects them; see ConfigParser.h.
bool parseUint16(std::string_view s, uint16_t& out) {
  if (s.empty() || !allOf(s, isDigit)) return false;
  uint32_t v = 0;
  for (char c : s) {
    v = v * 10 + static_cast<uint32_t>(c - '0');
    if (v > 0xffff) return false;
  }
  out = static_cast<uint16_t>(v);
  return true;
}

// A part that is the name of a value of E in `names` (QualifierNames.h), or "any" when
// `wildcard`. A value whose name is "" has no qualifier.
template <typename E, size_t N>
bool parseName(std::string_view part, E& out, const std::string_view (&names)[N],
               bool wildcard = true) {
  if (wildcard && part == "any"sv) {
    out = E{};
    return true;
  }
  for (size_t i = 1; i < N; i++) {
    if (!names[i].empty() && part == names[i]) {
      out = static_cast<E>(i);
      return true;
    }
  }
  return false;
}

// "<prefix><N><suffix>", for sw600dp, w720dp, h480dp.
bool parseDp(std::string_view part, std::string_view prefix, uint16_t& out) {
  if (part == "any"sv) {
    out = 0;
    return true;
  }
  return consume(part, prefix) && part.size() > 2 && part.substr(part.size() - 2) == "dp"sv &&
         parseUint16(part.substr(0, part.size() - 2), out);
}

bool parseMcc(std::string_view part, Config& c) {
  if (part == "any"sv) {
    c.mcc = 0;
    return true;
  }
  uint16_t v = 0;
  if (!consume(part, "mcc") || part.size() != 3 || !parseUint16(part, v) || v == 0) return false;
  c.mcc = v;
  return true;
}

bool parseMnc(std::string_view part, Config& c) {
  if (part == "any"sv) {
    c.mnc = 0;
    return true;
  }
  uint16_t v = 0;
  if (!consume(part, "mnc") || part.size() > 3 || !parseUint16(part, v)) return false;
  c.mnc = v == 0 ? kMncZero : v;
  return true;
}

template <size_t N>
void setChars(std::array<char, N>& field, std::string_view s, char (*transform)(char)) {
  field = {};
  for (size_t i = 0; i < s.size() && i < N; i++) field[i] = transform(s[i]);
}

char keep(char c) { return c; }

// The b+ form, after "b+": LocaleValue::InitFromBcp47TagImpl, which tells the subtags apart by
// their count and length alone.
bool parseBcp47(std::string_view tag, Config& c) {
  const std::vector<std::string> subtags = splitLower(tag, '+');
  for (const std::string& s : subtags) {
    if (s.empty() || !allOf(s, isAlnum)) return false;
  }
  std::string_view lang = subtags[0], script, region, variant;
  switch (subtags.size()) {
    case 1: break;
    case 2: {
      const std::string& s = subtags[1];
      if (s.size() == 2 || s.size() == 3) {
        region = s;
      } else if (s.size() == 4 && !isDigit(s[0])) {
        script = s;
      } else if (s.size() >= 4 && s.size() <= 8) {
        variant = s;
      } else {
        return false;
      }
      break;
    }
    case 3:
      if (subtags[1].size() == 4) {
        script = subtags[1];
      } else if (subtags[1].size() == 2 || subtags[1].size() == 3) {
        region = subtags[1];
      } else {
        return false;
      }
      // A short third subtag is the region, even after a region: b+de+CH+19 is de-r19.
      (subtags[2].size() >= 4 ? variant : region) = subtags[2];
      break;
    case 4:
      script = subtags[1];
      region = subtags[2];
      variant = subtags[3];
      break;
    default: return false;
  }
  // What aapt2 would cut short or pack into other characters.
  if (lang.size() < 2 || lang.size() > 3 || !allOf(lang, isLower)) return false;
  if (!region.empty() && region.size() != 2 && !(region.size() == 3 && allOf(region, isDigit))) {
    return false;
  }
  if (!script.empty() && script.size() != 4) return false;
  if (variant.size() > 8) return false;

  setChars(c.lang, lang, keep);
  setChars(c.region, region, toUpper);
  setChars(c.script, script, toLower);
  c.script[0] = toUpper(c.script[0]);
  setChars(c.variant, variant, keep);
  return true;
}

// LocaleValue::InitFromParts: the parts the locale takes, 0 for none, or -1 if the name fails.
int parseLocale(const std::vector<std::string>& parts, size_t i, Config& c) {
  const std::string& part = parts[i];
  if (part.size() >= 2 && part[0] == 'b' && part[1] == '+') {
    return parseBcp47(std::string_view(part).substr(2), c) ? 1 : -1;
  }
  if ((part.size() != 2 && part.size() != 3) || !allOf(part, isLower) || part == "car"sv) {
    return 0;
  }
  setChars(c.lang, part, keep);
  if (i + 1 < parts.size() && parts[i + 1].size() == 3 && parts[i + 1][0] == 'r') {
    const std::string_view region = std::string_view(parts[i + 1]).substr(1);
    // aapt2 takes any two characters here.
    if (!allOf(region, isAlnum)) return -1;
    setChars(c.region, region, toUpper);
    return 2;
  }
  return 1;
}

bool parseGrammaticalGender(std::string_view part, Config& c) {
  return parseName(part, c.grammaticalGender, names::kGrammaticalGender, /*wildcard=*/false);
}
bool parseLayoutDir(std::string_view part, Config& c) {
  return parseName(part, c.layoutDir, names::kLayoutDir);
}
bool parseSmallestWidth(std::string_view part, Config& c) {
  return parseDp(part, "sw", c.smallestWidthDp);
}
bool parseWidth(std::string_view part, Config& c) { return parseDp(part, "w", c.widthDp); }
bool parseHeight(std::string_view part, Config& c) { return parseDp(part, "h", c.heightDp); }
bool parseScreenSize(std::string_view part, Config& c) {
  return parseName(part, c.screenSize, names::kScreenSize);
}
bool parseScreenLong(std::string_view part, Config& c) {
  return parseName(part, c.screenLong, names::kScreenLong);
}
bool parseScreenRound(std::string_view part, Config& c) {
  return parseName(part, c.screenRound, names::kScreenRound);
}
bool parseWideColorGamut(std::string_view part, Config& c) {
  return parseName(part, c.wideColorGamut, names::kWideColorGamut);
}
bool parseHdr(std::string_view part, Config& c) { return parseName(part, c.hdr, names::kHdr); }
bool parseOrientation(std::string_view part, Config& c) {
  return parseName(part, c.orientation, names::kOrientation);
}
bool parseUiModeType(std::string_view part, Config& c) {
  return parseName(part, c.uiModeType, names::kUiModeType);
}
bool parseUiModeNight(std::string_view part, Config& c) {
  return parseName(part, c.uiModeNight, names::kUiModeNight);
}

bool parseDensity(std::string_view part, Config& c) {
  static constexpr struct {
    std::string_view name;
    uint16_t density;
  } kNames[] = {{"any", 0},
                {"anydpi", kDensityAny},
                {"nodpi", kDensityNone},
                {"ldpi", kDensityLdpi},
                {"mdpi", kDensityMdpi},
                {"tvdpi", kDensityTvdpi},
                {"hdpi", kDensityHdpi},
                {"xhdpi", kDensityXhdpi},
                {"xxhdpi", kDensityXxhdpi},
                {"xxxhdpi", kDensityXxxhdpi}};
  for (const auto& n : kNames) {
    if (part == n.name) {
      c.density = n.density;
      return true;
    }
  }
  uint16_t v = 0;
  if (part.size() <= 3 || part.substr(part.size() - 3) != "dpi"sv ||
      !parseUint16(part.substr(0, part.size() - 3), v) || v == 0) {
    return false;
  }
  c.density = v;  // 65534dpi is anydpi, 65535dpi nodpi, as in aapt2
  return true;
}

bool parseTouchscreen(std::string_view part, Config& c) {
  return parseName(part, c.touchscreen, names::kTouchscreen);
}
bool parseKeysHidden(std::string_view part, Config& c) {
  return parseName(part, c.keysHidden, names::kKeysHidden);
}
bool parseKeyboard(std::string_view part, Config& c) {
  return parseName(part, c.keyboard, names::kKeyboard);
}
bool parseNavHidden(std::string_view part, Config& c) {
  return parseName(part, c.navHidden, names::kNavHidden);
}
bool parseNavigation(std::string_view part, Config& c) {
  return parseName(part, c.navigation, names::kNavigation);
}

// <W>x<H> with W >= H. aapt2 lets H be empty: "640x" is 640x0.
bool parseScreenPixels(std::string_view part, Config& c) {
  if (part == "any"sv) {
    c.screenWidth = c.screenHeight = 0;
    return true;
  }
  const size_t x = part.find('x');
  uint16_t w = 0, h = 0;
  if (x == std::string_view::npos || !parseUint16(part.substr(0, x), w)) return false;
  const std::string_view height = part.substr(x + 1);
  if (!height.empty() && !parseUint16(height, h)) return false;
  if (w < h) return false;
  c.screenWidth = w;
  c.screenHeight = h;
  return true;
}

// v<N> or, from aapt2 36.1, v<N>.<minor>. Both numbers fail past 65535, as in aapt2.
bool parseVersion(std::string_view part, Config& c) {
  if (part == "any"sv) {
    c.sdkVersion = c.minorVersion = 0;
    return true;
  }
  if (!consume(part, "v")) return false;
  const size_t dot = part.find('.');
  uint16_t sdk = 0, minor = 0;
  if (!parseUint16(part.substr(0, dot), sdk)) return false;
  if (dot != std::string_view::npos && !parseUint16(part.substr(dot + 1), minor)) return false;
  if (sdk == 0 && minor != 0) return false;
  c.sdkVersion = sdk;
  c.minorVersion = minor;
  return true;
}

using QualifierParser = bool (*)(std::string_view, Config&);

constexpr QualifierParser kAfterLocale[] = {
    parseGrammaticalGender,
    parseLayoutDir,
    parseSmallestWidth,
    parseWidth,
    parseHeight,
    parseScreenSize,
    parseScreenLong,
    parseScreenRound,
    parseWideColorGamut,
    parseHdr,
    parseOrientation,
    parseUiModeType,
    parseUiModeNight,
    parseDensity,
    parseTouchscreen,
    parseKeysHidden,
    parseKeyboard,
    parseNavHidden,
    parseNavigation,
    parseScreenPixels,
    parseVersion,
};

// Android versions that brought in qualifiers (SDK_DONUT ... SDK_U, SDK_BAKLAVA).
constexpr uint16_t kDonut = 4, kFroyo = 8, kHoneycombMr2 = 13, kLollipop = 21, kMarshmallow = 23,
                   kOreo = 26, kU = 34, kBaklava = 36;

// ConfigDescription::ApplyVersionForCompatibility.
void applyVersionForCompatibility(Config& c) {
  uint16_t minSdk = 0;
  if (c.grammaticalGender != GrammaticalGender::Any) {
    minSdk = kU;
  } else if (c.uiModeType == UiModeType::VrHeadset || c.wideColorGamut != WideColorGamut::Any ||
             c.hdr != Hdr::Any) {
    minSdk = kOreo;
  } else if (c.screenRound != ScreenRound::Any) {
    minSdk = kMarshmallow;
  } else if (c.density == kDensityAny) {
    minSdk = kLollipop;
  } else if (c.smallestWidthDp != 0 || c.widthDp != 0 || c.heightDp != 0) {
    minSdk = kHoneycombMr2;
  } else if (c.uiModeType != UiModeType::Any || c.uiModeNight != UiModeNight::Any) {
    minSdk = kFroyo;
  } else if (c.screenSize != ScreenSize::Any || c.screenLong != ScreenLong::Any || c.density != 0) {
    minSdk = kDonut;
  }
  if (minSdk > c.sdkVersion) {
    c.sdkVersion = minSdk;
    c.minorVersion = 0;
  }
}

}  // namespace

std::optional<Config> parseConfig(std::string_view qualifiers) {
  Config c;
  if (!qualifiers.empty()) {
    const std::vector<std::string> parts = splitLower(qualifiers, '-');
    size_t i = 0;
    if (parseMcc(parts[i], c)) i++;
    if (i < parts.size() && parseMnc(parts[i], c)) i++;
    if (i < parts.size()) {
      const int taken = parseLocale(parts, i, c);
      if (taken < 0) return std::nullopt;
      i += static_cast<size_t>(taken);
    }
    for (QualifierParser parse : kAfterLocale) {
      if (i < parts.size() && parse(parts[i], c)) i++;
    }
    if (i != parts.size()) return std::nullopt;
  }
  applyVersionForCompatibility(c);
  // aapt2 compile's own check, after the version is raised.
  if (c.minorVersion != 0 && c.sdkVersion < kBaklava) return std::nullopt;
  return c;
}

std::optional<ResDir> parseResDir(std::string_view name) {
  const size_t dash = name.find('-');
  std::optional<Config> config =
      parseConfig(dash == std::string_view::npos ? std::string_view() : name.substr(dash + 1));
  if (!config) return std::nullopt;
  return ResDir{std::string(name.substr(0, dash)), *config};
}

std::optional<Config> parseLanguageTag(std::string_view tag) {
  const std::vector<std::string> subtags = splitLower(tag, '-');
  const auto letters = [](std::string_view s) { return allOf(s, isLower); };
  const auto digits = [](std::string_view s) { return allOf(s, isDigit); };

  size_t i = 0;
  const std::string& lang = subtags[i++];
  if (lang.size() < 2 || lang.size() > 3 || !letters(lang)) return std::nullopt;
  std::string_view script, region;
  if (i < subtags.size() && subtags[i].size() == 4 && letters(subtags[i])) script = subtags[i++];
  if (i < subtags.size() && ((subtags[i].size() == 2 && letters(subtags[i])) ||
                             (subtags[i].size() == 3 && digits(subtags[i])))) {
    region = subtags[i++];
  }
  Config c;
  for (; i < subtags.size(); i++) {
    const std::string& s = subtags[i];
    if (!allOf(s, isAlnum)) return std::nullopt;
    // An extension or private use, which goes on to the end of the tag.
    if (s.size() == 1) break;
    if (s.size() > 8 || s.size() < 4 || (s.size() == 4 && !isDigit(s[0]))) return std::nullopt;
    // setBcp47Locale writes each variant over the front of the one before, so the last is kept
    // with the tail of a longer one before it: ca-ES-valencia-1996 has "1996ncia".
    for (size_t k = 0; k < s.size(); k++) c.variant[k] = s[k];
  }

  setChars(c.lang, lang, keep);
  setChars(c.script, script, keep);
  c.script[0] = toUpper(c.script[0]);
  setChars(c.region, region, toUpper);
  return c;
}

}  // namespace hydroid::res

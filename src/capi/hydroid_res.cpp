#include "hydroid_res.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/FieldCount.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"
#include "hydroid/res/Version.h"

// Each function catches what the C++ API throws, which is only that it ran out of memory, as no
// exception may leave a C function: what calls it, C or Swift, cannot catch one.

namespace res = hydroid::res;

struct hydroid_res_table {
  res::ResourceTable table;
  std::string error;  // of the last addPackage
};

struct hydroid_res_resources {
  explicit hydroid_res_resources(const res::ResourceTable& t) : table(t), resources(t) {}

  const res::ResourceTable& table;
  res::Resources resources;
};

namespace {

using res::Config;
using res::ValueKind;

// hydroid_res_config has a field for each of Config's, and the conversions below copy them all.
static_assert(res::detail::fieldCount<Config>() == 29,
              "a field added to Config goes in hydroid_res_config and in toC and fromC too");

// The numbers of the C API are those of the C++ one.
static_assert(HYDROID_RES_VALUE_NULL == static_cast<int>(ValueKind::Null));
static_assert(HYDROID_RES_VALUE_EMPTY == static_cast<int>(ValueKind::Empty));
static_assert(HYDROID_RES_VALUE_ID == static_cast<int>(ValueKind::Id));
static_assert(HYDROID_RES_VALUE_REFERENCE == static_cast<int>(ValueKind::Reference));
static_assert(HYDROID_RES_VALUE_ATTR_REF == static_cast<int>(ValueKind::AttrRef));
static_assert(HYDROID_RES_VALUE_STRING == static_cast<int>(ValueKind::String));
static_assert(HYDROID_RES_VALUE_STYLED_STRING == static_cast<int>(ValueKind::StyledString));
static_assert(HYDROID_RES_VALUE_COLOR == static_cast<int>(ValueKind::Color));
static_assert(HYDROID_RES_VALUE_DIMENSION == static_cast<int>(ValueKind::Dimension));
static_assert(HYDROID_RES_VALUE_FRACTION == static_cast<int>(ValueKind::Fraction));
static_assert(HYDROID_RES_VALUE_FLOAT == static_cast<int>(ValueKind::Float));
static_assert(HYDROID_RES_VALUE_INT == static_cast<int>(ValueKind::Int));
static_assert(HYDROID_RES_VALUE_BOOL == static_cast<int>(ValueKind::Bool));
static_assert(HYDROID_RES_VALUE_FILE == static_cast<int>(ValueKind::File));
static_assert(HYDROID_RES_VALUE_STYLE == static_cast<int>(ValueKind::Style));
static_assert(HYDROID_RES_VALUE_ARRAY == static_cast<int>(ValueKind::Array));
static_assert(HYDROID_RES_VALUE_PLURALS == static_cast<int>(ValueKind::Plurals));
static_assert(HYDROID_RES_VALUE_ATTR_DECL == static_cast<int>(ValueKind::AttrDecl));
static_assert(HYDROID_RES_VALUE_STYLEABLE_DECL == static_cast<int>(ValueKind::StyleableDecl));
static_assert(static_cast<int>(res::ColorFormat::Argb8) == 28 &&
              static_cast<int>(res::ColorFormat::Rgb4) == 31);
static_assert(static_cast<int>(res::IntFormat::Dec) == 16 &&
              static_cast<int>(res::IntFormat::Hex) == 17);
static_assert(static_cast<int>(res::FileType::Other) == 0 &&
              static_cast<int>(res::FileType::Xml) == 1 &&
              static_cast<int>(res::FileType::Png) == 2);

template <size_t N>
void copyChars(const char (&from)[N], std::array<char, N>& to) {
  std::memcpy(to.data(), from, N);
}
template <size_t N>
void copyChars(const std::array<char, N>& from, char (&to)[N]) {
  std::memcpy(to, from.data(), N);
}

Config fromC(const hydroid_res_config& c) {
  Config out;
  out.mcc = c.mcc;
  out.mnc = c.mnc;
  copyChars(c.lang, out.lang);
  copyChars(c.region, out.region);
  copyChars(c.script, out.script);
  copyChars(c.variant, out.variant);
  out.grammaticalGender = static_cast<res::GrammaticalGender>(c.grammatical_gender);
  out.layoutDir = static_cast<res::LayoutDir>(c.layout_dir);
  out.smallestWidthDp = c.smallest_width_dp;
  out.widthDp = c.width_dp;
  out.heightDp = c.height_dp;
  out.screenSize = static_cast<res::ScreenSize>(c.screen_size);
  out.screenLong = static_cast<res::ScreenLong>(c.screen_long);
  out.screenRound = static_cast<res::ScreenRound>(c.screen_round);
  out.wideColorGamut = static_cast<res::WideColorGamut>(c.wide_color_gamut);
  out.hdr = static_cast<res::Hdr>(c.hdr);
  out.orientation = static_cast<res::Orientation>(c.orientation);
  out.uiModeType = static_cast<res::UiModeType>(c.ui_mode_type);
  out.uiModeNight = static_cast<res::UiModeNight>(c.ui_mode_night);
  out.density = c.density;
  out.touchscreen = static_cast<res::Touchscreen>(c.touchscreen);
  out.keysHidden = static_cast<res::KeysHidden>(c.keys_hidden);
  out.keyboard = static_cast<res::Keyboard>(c.keyboard);
  out.navHidden = static_cast<res::NavHidden>(c.nav_hidden);
  out.navigation = static_cast<res::Navigation>(c.navigation);
  out.screenWidth = c.screen_width;
  out.screenHeight = c.screen_height;
  out.sdkVersion = c.sdk_version;
  out.minorVersion = c.minor_version;
  return out;
}

hydroid_res_config toC(const Config& c) {
  hydroid_res_config out{};
  out.mcc = c.mcc;
  out.mnc = c.mnc;
  copyChars(c.lang, out.lang);
  copyChars(c.region, out.region);
  copyChars(c.script, out.script);
  copyChars(c.variant, out.variant);
  out.grammatical_gender = static_cast<uint8_t>(c.grammaticalGender);
  out.layout_dir = static_cast<uint8_t>(c.layoutDir);
  out.smallest_width_dp = c.smallestWidthDp;
  out.width_dp = c.widthDp;
  out.height_dp = c.heightDp;
  out.screen_size = static_cast<uint8_t>(c.screenSize);
  out.screen_long = static_cast<uint8_t>(c.screenLong);
  out.screen_round = static_cast<uint8_t>(c.screenRound);
  out.wide_color_gamut = static_cast<uint8_t>(c.wideColorGamut);
  out.hdr = static_cast<uint8_t>(c.hdr);
  out.orientation = static_cast<uint8_t>(c.orientation);
  out.ui_mode_type = static_cast<uint8_t>(c.uiModeType);
  out.ui_mode_night = static_cast<uint8_t>(c.uiModeNight);
  out.density = c.density;
  out.touchscreen = static_cast<uint8_t>(c.touchscreen);
  out.keys_hidden = static_cast<uint8_t>(c.keysHidden);
  out.keyboard = static_cast<uint8_t>(c.keyboard);
  out.nav_hidden = static_cast<uint8_t>(c.navHidden);
  out.navigation = static_cast<uint8_t>(c.navigation);
  out.screen_width = c.screenWidth;
  out.screen_height = c.screenHeight;
  out.sdk_version = c.sdkVersion;
  out.minor_version = c.minorVersion;
  return out;
}

void setString(hydroid_res_value& out, const std::string& s) {
  out.string = s.c_str();
  out.string_length = s.size();
}

// `value` of the resource `id`, whose package holds the pools it points into.
hydroid_res_value toC(const res::ResourceTable& table, res::ResId id, const res::Value& value) {
  hydroid_res_value out{};
  out.kind = static_cast<hydroid_res_value_kind>(value.kind());
  const res::Package* package = table.package(res::packageId(id));
  switch (value.kind()) {
    case ValueKind::Reference:
    case ValueKind::AttrRef: out.data = value.ref(); break;
    case ValueKind::Color:
      out.data = value.argb();
      out.format = static_cast<uint8_t>(value.colorFormat());
      break;
    case ValueKind::Int:
      out.data = static_cast<uint32_t>(value.intValue());
      out.format = static_cast<uint8_t>(value.intFormat());
      break;
    case ValueKind::Bool: out.data = value.boolValue() ? 1 : 0; break;
    case ValueKind::Float: {
      const float f = value.floatValue();
      static_assert(sizeof f == sizeof out.data);
      std::memcpy(&out.data, &f, sizeof f);
      break;
    }
    case ValueKind::Dimension:
    case ValueKind::Fraction: out.data = value.complexData(); break;
    case ValueKind::String:
      if (package) setString(out, package->strings[value.index()]);
      break;
    case ValueKind::StyledString:
      if (package) setString(out, package->styledStrings[value.index()].text);
      break;
    case ValueKind::File:
      if (package) {
        const res::FileRef& file = package->files[value.index()];
        setString(out, file.path);
        out.format = static_cast<uint8_t>(file.type);
        out.source = file.source.c_str();
        out.source_length = file.source.size();
      }
      break;
    case ValueKind::Null:
    case ValueKind::Empty:
    case ValueKind::Id:
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl: break;
  }
  return out;
}

// What addPackage said, as hydroid_res_table_error gives it. The lines are the same in any
// locale.
std::string describeErrors(const std::vector<res::Diagnostic>& errors) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  for (const res::Diagnostic& d : errors) out << d << '\n';
  return out.str();
}

}  // namespace

extern "C" {

const char* hydroid_res_version(void) { return res::version(); }

hydroid_res_table* hydroid_res_table_create(void) {
  try {
    return new hydroid_res_table();
  } catch (...) {
    return nullptr;
  }
}

void hydroid_res_table_destroy(hydroid_res_table* table) { delete table; }

int hydroid_res_table_add_package(hydroid_res_table* table, const char* folder, const char* name,
                                  uint8_t id) {
  table->error.clear();
  try {
    // A path that is not UTF-8 names no folder: u8path throws on it on Windows, and elsewhere
    // takes its bytes as they are, which is a folder of its own.
    std::filesystem::path path;
    try {
      path = std::filesystem::u8path(folder);
    } catch (const std::system_error&) {
      table->error = "the path of the res/ folder is not UTF-8\n";
      return 0;
    }
    std::vector<res::Diagnostic> errors;
    if (table->table.addPackage(path, name, id, &errors)) return 1;
    table->error = describeErrors(errors);
    return 0;
  } catch (...) {
    // Out of memory, so the error may not fit either; one that does says so.
    try {
      table->error = "out of memory\n";
    } catch (...) {
    }
    return 0;
  }
}

const char* hydroid_res_table_error(const hydroid_res_table* table) { return table->error.c_str(); }

uint32_t hydroid_res_table_find(const hydroid_res_table* table, const char* name,
                                const char* default_package) {
  try {
    return table->table.find(name, default_package ? default_package : std::string_view())
        .value_or(0);
  } catch (...) {
    return 0;
  }
}

hydroid_res_resources* hydroid_res_resources_create(const hydroid_res_table* table) {
  try {
    return new hydroid_res_resources(table->table);
  } catch (...) {
    return nullptr;
  }
}

void hydroid_res_resources_destroy(hydroid_res_resources* resources) { delete resources; }

int hydroid_res_resources_set_configuration(hydroid_res_resources* resources,
                                            const hydroid_res_config* device,
                                            const char* const* locales, size_t locale_count) {
  try {
    std::vector<Config> tags;
    tags.reserve(locale_count);
    for (size_t i = 0; i < locale_count; i++) {
      const std::optional<Config> locale = res::parseLanguageTag(locales[i]);
      if (!locale) return 0;
      tags.push_back(*locale);
    }
    resources->resources.setConfiguration(fromC(*device), tags);
    return 1;
  } catch (...) {
    // setConfiguration changes nothing if it throws (Resources.cpp).
    return 0;
  }
}

// Both take in a change of the table, which allocates (Resources.cpp, syncWithTable).
int hydroid_res_resources_configuration(const hydroid_res_resources* resources,
                                        hydroid_res_config* out) {
  try {
    *out = toC(resources->resources.configuration());
    return 1;
  } catch (...) {
    return 0;
  }
}

int hydroid_res_resources_generation(const hydroid_res_resources* resources, uint32_t* out) {
  try {
    *out = resources->resources.generation();
    return 1;
  } catch (...) {
    return 0;
  }
}

int hydroid_res_resources_value(const hydroid_res_resources* resources, uint32_t id,
                                hydroid_res_value* out, uint32_t* generation) {
  return hydroid_res_resources_value_for_density(resources, id, 0, out, generation);
}

int hydroid_res_resources_value_for_density(const hydroid_res_resources* resources, uint32_t id,
                                            uint16_t density, hydroid_res_value* out,
                                            uint32_t* generation) {
  const res::Candidate* candidate;
  uint32_t of = 0;
  try {
    candidate = resources->resources.candidateForDensity(id, density, &of);
  } catch (...) {
    return -1;
  }
  if (generation) *generation = of;
  if (!candidate) {
    *out = hydroid_res_value{};
    return 0;
  }
  *out = toC(resources->table, id, candidate->value);
  return 1;
}

}  // extern "C"

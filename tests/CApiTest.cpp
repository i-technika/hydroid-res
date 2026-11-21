// The C API (hydroid_res.h, #203): that it loads a table, sets the configuration of a device and
// gives the values the C++ API picks for it, as they are, and that what fails says so rather than
// throwing. CApiCTest.c calls it from C.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "TempDir.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"
#include "hydroid_res.h"

namespace hydroid::res {
namespace {

using test::TempDir;

constexpr const char* kTablePackage = "com.itechnika.hydroid.table";

// A device with every field of Config, as KeepsEveryFieldOfTheDevice sets it in C.
constexpr const char* kEveryField =
    "mcc310-mnc260-b+sr+Latn+RS+1901-feminine-ldrtl-sw600dp-w960dp-h600dp-large-long-notround-"
    "widecg-lowdr-land-car-night-xxhdpi-finger-keyssoft-qwerty-navhidden-dpad-2400x1080-v36.1";

// The C handles, freed at the end of their scope.
struct CTable {
  CTable() : table(hydroid_res_table_create()) {}
  ~CTable() { hydroid_res_table_destroy(table); }
  CTable(const CTable&) = delete;
  CTable& operator=(const CTable&) = delete;
  hydroid_res_table* table;
};
struct CResources {
  explicit CResources(const hydroid_res_table* table)
      : resources(hydroid_res_resources_create(table)) {}
  ~CResources() { hydroid_res_resources_destroy(resources); }
  CResources(const CResources&) = delete;
  CResources& operator=(const CResources&) = delete;
  hydroid_res_resources* resources;
};

// The C configuration of `c`, written here field by field rather than taken from the library, so
// that a field the library leaves out of its conversion shows.
hydroid_res_config toC(const Config& c) {
  hydroid_res_config out{};
  out.mcc = c.mcc;
  out.mnc = c.mnc;
  std::memcpy(out.lang, c.lang.data(), sizeof out.lang);
  std::memcpy(out.region, c.region.data(), sizeof out.region);
  std::memcpy(out.script, c.script.data(), sizeof out.script);
  std::memcpy(out.variant, c.variant.data(), sizeof out.variant);
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

// Every field of `a` and `b`, by name, so that a failure says which differs.
void expectSameConfig(const hydroid_res_config& a, const hydroid_res_config& b) {
  // It binds 29 names, as Config has 29 fields, and fails to build once there are more: one added
  // goes here too.
  [[maybe_unused]] const auto& [f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15,
                                f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28,
                                f29] = a;
#define HYDROID_EXPECT_FIELD(f) EXPECT_EQ(a.f, b.f) << #f
  HYDROID_EXPECT_FIELD(mcc);
  HYDROID_EXPECT_FIELD(mnc);
  EXPECT_EQ(std::string(a.lang, 4), std::string(b.lang, 4)) << "lang";
  EXPECT_EQ(std::string(a.region, 4), std::string(b.region, 4)) << "region";
  EXPECT_EQ(std::string(a.script, 4), std::string(b.script, 4)) << "script";
  EXPECT_EQ(std::string(a.variant, 8), std::string(b.variant, 8)) << "variant";
  HYDROID_EXPECT_FIELD(grammatical_gender);
  HYDROID_EXPECT_FIELD(layout_dir);
  HYDROID_EXPECT_FIELD(smallest_width_dp);
  HYDROID_EXPECT_FIELD(width_dp);
  HYDROID_EXPECT_FIELD(height_dp);
  HYDROID_EXPECT_FIELD(screen_size);
  HYDROID_EXPECT_FIELD(screen_long);
  HYDROID_EXPECT_FIELD(screen_round);
  HYDROID_EXPECT_FIELD(wide_color_gamut);
  HYDROID_EXPECT_FIELD(hdr);
  HYDROID_EXPECT_FIELD(orientation);
  HYDROID_EXPECT_FIELD(ui_mode_type);
  HYDROID_EXPECT_FIELD(ui_mode_night);
  HYDROID_EXPECT_FIELD(density);
  HYDROID_EXPECT_FIELD(touchscreen);
  HYDROID_EXPECT_FIELD(keys_hidden);
  HYDROID_EXPECT_FIELD(keyboard);
  HYDROID_EXPECT_FIELD(nav_hidden);
  HYDROID_EXPECT_FIELD(navigation);
  HYDROID_EXPECT_FIELD(screen_width);
  HYDROID_EXPECT_FIELD(screen_height);
  HYDROID_EXPECT_FIELD(sdk_version);
  HYDROID_EXPECT_FIELD(minor_version);
#undef HYDROID_EXPECT_FIELD
}

Config cfg(const std::string& qualifiers) {
  const std::optional<Config> c = parseConfig(qualifiers);
  EXPECT_TRUE(c) << qualifiers;
  return c.value_or(Config{});
}

uint32_t generationOf(const hydroid_res_resources* resources) {
  uint32_t generation = 0;
  EXPECT_EQ(hydroid_res_resources_generation(resources, &generation), 1);
  return generation;
}

hydroid_res_config configurationOf(const hydroid_res_resources* resources) {
  hydroid_res_config config{};
  EXPECT_EQ(hydroid_res_resources_configuration(resources, &config), 1);
  return config;
}

std::string text(const hydroid_res_value& v) {
  return v.string ? std::string(v.string, v.string_length) : std::string("(none)");
}

// `got`, the C value of `want` of `package`, holds what `want` does.
void expectSameValue(const Package& package, const Value& want, const hydroid_res_value& got,
                     const std::string& what) {
  SCOPED_TRACE(what);
  ASSERT_EQ(static_cast<int>(got.kind), static_cast<int>(want.kind()));
  uint32_t data = 0;
  uint8_t format = 0;
  const std::string* string = nullptr;
  switch (want.kind()) {
    case ValueKind::Reference:
    case ValueKind::AttrRef: data = want.ref(); break;
    case ValueKind::Color:
      data = want.argb();
      format = static_cast<uint8_t>(want.colorFormat());
      break;
    case ValueKind::Int:
      data = static_cast<uint32_t>(want.intValue());
      format = static_cast<uint8_t>(want.intFormat());
      break;
    case ValueKind::Bool: data = want.boolValue(); break;
    case ValueKind::Float: {
      const float f = want.floatValue();
      std::memcpy(&data, &f, sizeof data);
      break;
    }
    case ValueKind::Dimension:
    case ValueKind::Fraction: data = want.complexData(); break;
    case ValueKind::String: string = &package.strings[want.index()]; break;
    case ValueKind::StyledString: string = &package.styledStrings[want.index()].text; break;
    case ValueKind::File:
      string = &package.files[want.index()].path;
      format = static_cast<uint8_t>(package.files[want.index()].type);
      break;
    default: break;
  }
  EXPECT_EQ(got.data, data);
  EXPECT_EQ(got.format, format);
  if (string) {
    ASSERT_NE(got.string, nullptr);
    EXPECT_EQ(std::string(got.string, got.string_length), *string);
    EXPECT_EQ(got.string[got.string_length], '\0');
  } else {
    EXPECT_EQ(got.string, nullptr);
    EXPECT_EQ(got.string_length, 0u);
  }
}

// The C table of the golden table/res, which must load.
void addTable(hydroid_res_table* table) {
  ASSERT_EQ(hydroid_res_table_add_package(table, HYDROID_TABLE_RES_DIR, kTablePackage, 0x7f), 1)
      << hydroid_res_table_error(table);
  EXPECT_STREQ(hydroid_res_table_error(table), "");
}

TEST(CApi, FindsResourcesByName) {
  CTable c;
  ASSERT_NE(c.table, nullptr);
  addTable(c.table);
  ResourceTable table;
  ASSERT_TRUE(table.addPackage(HYDROID_TABLE_RES_DIR, kTablePackage, kAppPackageId));
  const uint32_t id = table.find(kTablePackage, ResType::String, "app_name").value();
  EXPECT_EQ(hydroid_res_table_find(c.table, "com.itechnika.hydroid.table:string/app_name", nullptr),
            id);
  EXPECT_EQ(hydroid_res_table_find(c.table, "@string/app_name", kTablePackage), id);
  EXPECT_EQ(hydroid_res_table_find(c.table, "@string/app_name", nullptr), 0u);
  EXPECT_EQ(hydroid_res_table_find(c.table, "@string/no_such_name", kTablePackage), 0u);
  EXPECT_EQ(hydroid_res_table_find(c.table, "", nullptr), 0u);
}

// What failed is in hydroid_res_table_error until the next load, and the table holds what it did.
TEST(CApi, SaysWhyAPackageFailed) {
  TempDir dir;
  dir.write("bad/values/strings.xml",
            "<resources><string name=\"a\">x</string><string name=\"a\">y</string></resources>");
  dir.write("good/values/strings.xml", "<resources><string name=\"a\">x</string></resources>");
  const std::string bad = (dir.path() / "bad").u8string();
  const std::string good = (dir.path() / "good").u8string();

  CTable c;
  EXPECT_STREQ(hydroid_res_table_error(c.table), "");
  EXPECT_EQ(hydroid_res_table_add_package(c.table, bad.c_str(), "com.example", 0x7f), 0);
  const std::string error = hydroid_res_table_error(c.table);
  EXPECT_NE(error.find("strings.xml:1: "), std::string::npos) << error;
  ASSERT_FALSE(error.empty());
  EXPECT_EQ(error.back(), '\n');
  EXPECT_EQ(hydroid_res_table_find(c.table, "com.example:string/a", nullptr), 0u);

  EXPECT_EQ(hydroid_res_table_add_package(c.table, good.c_str(), "com.example", 0), 0);
  EXPECT_STREQ(hydroid_res_table_error(c.table), "a package needs a name and an id other than 0\n");

  EXPECT_EQ(hydroid_res_table_add_package(c.table, good.c_str(), "com.example", 0x7f), 1);
  EXPECT_STREQ(hydroid_res_table_error(c.table), "");
  EXPECT_NE(hydroid_res_table_find(c.table, "com.example:string/a", nullptr), 0u);

  const std::string missing = (dir.path() / "missing").u8string();
  EXPECT_EQ(hydroid_res_table_add_package(c.table, missing.c_str(), "com.other", 0x7e), 0);
  EXPECT_NE(std::string(hydroid_res_table_error(c.table)), "");
}

// A path is UTF-8, and one that is not is an error, not an exception, on Windows, where it can
// name no folder; elsewhere it is bytes, and these name no folder that is there.
TEST(CApi, TakesAPathThatIsNotUtf8) {
  CTable c;
  EXPECT_EQ(hydroid_res_table_add_package(c.table, "\xff\xfe/res", "com.example", 0x7f), 0);
#ifdef _WIN32
  EXPECT_STREQ(hydroid_res_table_error(c.table), "the path of the res/ folder is not UTF-8\n");
#else
  EXPECT_NE(std::string(hydroid_res_table_error(c.table)), "");
#endif
}

// Each value of the table is what the C++ API picks, under some devices: every kind it holds.
TEST(CApi, GivesTheValuesTheCppApiPicks) {
  CTable c;
  addTable(c.table);
  CResources r(c.table);
  ASSERT_NE(r.resources, nullptr);
  ResourceTable table;
  ASSERT_TRUE(table.addPackage(HYDROID_TABLE_RES_DIR, kTablePackage, kAppPackageId));
  Resources resources(table);
  const Package& package = table.packages().front();

  struct Device {
    std::string qualifiers;
    std::vector<const char*> locales;
  };
  std::vector<Device> devices = {
      {"", {}},
      {"ko-rKR-land-night-xxhdpi", {}},
      {"sw600dp-hdpi", {"fr-CA", "ko"}},
      {"mcc310-mnc260-en-rUS-port-v21", {"de-CH"}},
      {kEveryField, {}},
  };
  // And one of each values-<qualifiers>/, which then decide config_order where they come before
  // the density: a field the conversions lose, or two they swap, picks another (w720dp/ for
  // w720dp, h480dp/ if swapped). values-anydpi/ is picked before those after the density, which
  // PicksByEachFieldAfterTheDensity has a table of their own for.
  for (const auto& folder : std::filesystem::directory_iterator(HYDROID_TABLE_RES_DIR)) {
    const std::string name = folder.path().filename().string();
    if (name.rfind("values-", 0) == 0) devices.push_back({name.substr(7), {}});
  }
  ASSERT_GT(devices.size(), 50u);
  std::vector<int> kinds(HYDROID_RES_VALUE_STYLEABLE_DECL + 1);
  for (const Device& device : devices) {
    SCOPED_TRACE(device.qualifiers);
    const Config config = cfg(device.qualifiers);
    std::vector<Config> locales;
    for (const char* tag : device.locales) locales.push_back(parseLanguageTag(tag).value());
    resources.setConfiguration(config, locales);
    const hydroid_res_config cConfig = toC(config);
    ASSERT_EQ(hydroid_res_resources_set_configuration(r.resources, &cConfig, device.locales.data(),
                                                      device.locales.size()),
              1);
    expectSameConfig(configurationOf(r.resources), toC(resources.configuration()));

    for (const TypeTable& type : package.types) {
      for (const Entry& entry : type.entries) {
        const ResId id = makeResId(package.id, type.id, entry.id);
        const Candidate* want = resources.candidate(id);
        hydroid_res_value value;
        uint32_t generation = 0xdead;
        const int found = hydroid_res_resources_value(r.resources, id, &value, &generation);
        EXPECT_EQ(generation, generationOf(r.resources));
        const std::string what = std::string(toString(type.type)) + "/" + entry.name;
        if (!want) {
          EXPECT_EQ(found, 0) << what;
          continue;
        }
        ASSERT_EQ(found, 1) << what;
        expectSameValue(package, want->value, value, what);
        kinds[value.kind]++;
      }
    }
  }
  for (size_t kind = 0; kind < kinds.size(); kind++) {
    EXPECT_GT(kinds[kind], 0) << "no value of kind " << kind;
  }
}

// The fields after the density, in a table with no density folder, so that each folder's device
// picks that folder: two of them the conversions swap pick another (qwerty/ for dpad, if the
// keyboard and the navigation were swapped).
TEST(CApi, PicksByEachFieldAfterTheDensity) {
  const std::vector<std::string> folders = {
      "notouch", "stylus", "finger", "keysexposed", "keyshidden", "keyssoft", "nokeys",
      "qwerty",  "12key",  "nonav",  "dpad",        "trackball",  "wheel"};
  TempDir dir;
  dir.write("values/strings.xml", "<resources><string name=\"where\">default</string></resources>");
  for (const std::string& folder : folders) {
    dir.write("values-" + folder + "/strings.xml",
              "<resources><string name=\"where\">" + folder + "</string></resources>");
  }
  CTable c;
  ASSERT_EQ(
      hydroid_res_table_add_package(c.table, dir.path().u8string().c_str(), "com.example", 0x7f), 1)
      << hydroid_res_table_error(c.table);
  CResources r(c.table);
  const uint32_t where = hydroid_res_table_find(c.table, "com.example:string/where", nullptr);
  for (const std::string& folder : folders) {
    SCOPED_TRACE(folder);
    const hydroid_res_config device = toC(cfg(folder));
    ASSERT_EQ(hydroid_res_resources_set_configuration(r.resources, &device, nullptr, 0), 1);
    hydroid_res_value v{};
    ASSERT_EQ(hydroid_res_resources_value(r.resources, where, &v, nullptr), 1);
    EXPECT_EQ(text(v), folder);
  }
}

// What the values are, written out, so that a C caller can read them by the header alone.
TEST(CApi, GivesValuesAsTheHeaderSays) {
  CTable c;
  addTable(c.table);
  CResources r(c.table);
  const auto value = [&](const char* name) {
    hydroid_res_value v{};
    const uint32_t id = hydroid_res_table_find(c.table, name, kTablePackage);
    EXPECT_NE(id, 0u) << name;
    EXPECT_EQ(hydroid_res_resources_value(r.resources, id, &v, nullptr), 1) << name;
    return v;
  };
  hydroid_res_value v = value("@color/rgb4");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_COLOR);
  EXPECT_EQ(v.data, 0xffff8800u);
  EXPECT_EQ(v.format, 31);
  v = value("@color/argb8");
  EXPECT_EQ(v.data, 0x806200eeu);
  EXPECT_EQ(v.format, 28);
  v = value("@color/alias");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_REFERENCE);
  EXPECT_EQ(v.data, hydroid_res_table_find(c.table, "@color/rgb8", kTablePackage));
  v = value("@color/theme_color");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_ATTR_REF);
  EXPECT_EQ(v.data, hydroid_res_table_find(c.table, "@attr/colorAttr", kTablePackage));
  v = value("@integer/neg");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_INT);
  EXPECT_EQ(static_cast<int32_t>(v.data), -7);
  EXPECT_EQ(v.format, 16);
  v = value("@integer/hex");
  EXPECT_EQ(v.data, 0x7fu);
  EXPECT_EQ(v.format, 17);
  v = value("@bool/yes");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_BOOL);
  EXPECT_EQ(v.data, 1u);
  v = value("@dimen/item_float");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_FLOAT);
  float f;
  std::memcpy(&f, &v.data, sizeof f);
  EXPECT_EQ(f, 0.25f);
  v = value("@dimen/dp");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_DIMENSION);
  EXPECT_EQ(v.data, 0x1001u);  // 16, of unit 1, dp
  v = value("@string/korean");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_STRING);
  EXPECT_EQ(text(v), "\xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94");  // 안녕하세요
  v = value("@string/styled");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_STYLED_STRING);
  EXPECT_EQ(text(v), "a bold, italic under and red");
  v = value("@drawable/icon");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_FILE);
  EXPECT_EQ(text(v), "res/drawable/icon.png");
  EXPECT_EQ(v.format, 2);
  v = value("@raw/data");
  EXPECT_EQ(text(v), "res/raw/data.bin");
  EXPECT_EQ(v.format, 0);
  v = value("@style/Base");
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_STYLE);
  EXPECT_EQ(v.data, 0u);
  EXPECT_EQ(v.string, nullptr);
}

TEST(CApi, GivesTheValueForAnotherDensity) {
  CTable c;
  addTable(c.table);
  CResources r(c.table);
  const uint32_t icon = hydroid_res_table_find(c.table, "@drawable/icon", kTablePackage);
  hydroid_res_value v{};
  EXPECT_EQ(hydroid_res_resources_value_for_density(r.resources, icon, 240, &v, nullptr), 1);
  EXPECT_EQ(text(v), "res/drawable-hdpi-v4/icon.png");
  EXPECT_EQ(hydroid_res_resources_value_for_density(r.resources, icon, 0, &v, nullptr), 1);
  EXPECT_EQ(text(v), "res/drawable/icon.png");
}

TEST(CApi, SaysWhenThereIsNoValue) {
  CTable c;
  addTable(c.table);
  CResources r(c.table);
  hydroid_res_value v;
  v.kind = HYDROID_RES_VALUE_STRING;
  v.data = 1;
  v.format = 1;
  v.string = "stale";
  v.string_length = 5;
  uint32_t generation = 0xdead;
  EXPECT_EQ(hydroid_res_resources_value(r.resources, 0x7f7f0000, &v, &generation), 0);
  EXPECT_EQ(v.kind, HYDROID_RES_VALUE_NULL);
  EXPECT_EQ(v.data, 0u);
  EXPECT_EQ(v.format, 0);
  EXPECT_EQ(v.string, nullptr);
  EXPECT_EQ(v.string_length, 0u);
  EXPECT_EQ(generation, generationOf(r.resources));
  EXPECT_EQ(hydroid_res_resources_value(r.resources, 0, &v, nullptr), 0);
}

// The user's languages are language tags, of which the table's is chosen; one that is not a tag
// changes nothing. The generation follows the configuration and the table.
TEST(CApi, TakesTheUsersLanguages) {
  CTable c;
  addTable(c.table);
  CResources r(c.table);
  const uint32_t order = hydroid_res_table_find(c.table, "@string/config_order", kTablePackage);
  hydroid_res_value v{};
  ASSERT_EQ(hydroid_res_resources_value(r.resources, order, &v, nullptr), 1);
  EXPECT_EQ(text(v), "anydpi");  // values-anydpi/ comes before all (Resources.h)

  const hydroid_res_config device{};
  const char* locales[] = {"xx-YY", "ko-KR"};
  const uint32_t before = generationOf(r.resources);
  ASSERT_EQ(hydroid_res_resources_set_configuration(r.resources, &device, locales, 2), 1);
  const uint32_t after = generationOf(r.resources);
  EXPECT_NE(after, before);
  ASSERT_EQ(hydroid_res_resources_value(r.resources, order, &v, nullptr), 1);
  EXPECT_EQ(text(v), "ko-rKR");
  const hydroid_res_config got = configurationOf(r.resources);
  EXPECT_EQ(std::string(got.lang, 4), std::string("ko\0\0", 4));
  EXPECT_EQ(std::string(got.region, 4), std::string("KR\0\0", 4));

  const char* bad[] = {"ko-KR", "not a tag"};
  EXPECT_EQ(hydroid_res_resources_set_configuration(r.resources, &device, bad, 2), 0);
  EXPECT_EQ(generationOf(r.resources), after);
  ASSERT_EQ(hydroid_res_resources_value(r.resources, order, &v, nullptr), 1);
  EXPECT_EQ(text(v), "ko-rKR");

  EXPECT_EQ(hydroid_res_resources_set_configuration(r.resources, &device, nullptr, 0), 1);
  ASSERT_EQ(hydroid_res_resources_value(r.resources, order, &v, nullptr), 1);
  EXPECT_EQ(text(v), "anydpi");

  // A package added after changes the generation.
  TempDir dir;
  dir.write("values/strings.xml", "<resources><string name=\"a\">x</string></resources>");
  const uint32_t beforeTable = generationOf(r.resources);
  ASSERT_EQ(
      hydroid_res_table_add_package(c.table, dir.path().u8string().c_str(), "com.other", 0x7e), 1);
  ASSERT_EQ(hydroid_res_resources_value(r.resources, order, &v, nullptr), 1);
  EXPECT_NE(generationOf(r.resources), beforeTable);
}

// Each field of the device goes through, and comes back as setConfiguration makes it.
TEST(CApi, KeepsEveryFieldOfTheDevice) {
  CTable c;
  addTable(c.table);
  CResources r(c.table);
  hydroid_res_config device{};
  device.mcc = 310;
  device.mnc = 260;
  std::memcpy(device.lang, "sr", 2);
  std::memcpy(device.region, "RS", 2);
  std::memcpy(device.script, "Latn", 4);
  std::memcpy(device.variant, "1901", 4);
  device.grammatical_gender = 2;
  device.layout_dir = 2;
  device.smallest_width_dp = 600;
  device.width_dp = 960;
  device.height_dp = 600;
  device.screen_size = 3;
  device.screen_long = 2;
  device.screen_round = 1;
  device.wide_color_gamut = 2;
  device.hdr = 1;
  device.orientation = 2;
  device.ui_mode_type = 3;
  device.ui_mode_night = 2;
  device.density = 480;
  device.touchscreen = 3;
  device.keys_hidden = 3;
  device.keyboard = 2;
  device.nav_hidden = 2;
  device.navigation = 2;
  device.screen_width = 2400;
  device.screen_height = 1080;
  device.sdk_version = 36;
  device.minor_version = 1;
  ASSERT_EQ(hydroid_res_resources_set_configuration(r.resources, &device, nullptr, 0), 1);
  hydroid_res_config got;
  std::memset(&got, 0x5a, sizeof got);
  ASSERT_EQ(hydroid_res_resources_configuration(r.resources, &got), 1);
  hydroid_res_config want = device;
  want.nav_hidden = 0;  // as Android tells its resources nothing of it
  expectSameConfig(got, want);

  // As the C++ API has it, by toC here.
  ResourceTable table;
  ASSERT_TRUE(table.addPackage(HYDROID_TABLE_RES_DIR, kTablePackage, kAppPackageId));
  Resources resources(table);
  resources.setConfiguration(cfg(kEveryField));
  expectSameConfig(got, toC(resources.configuration()));
}

TEST(CApi, DestroysNothingOfNull) {
  hydroid_res_table_destroy(nullptr);
  hydroid_res_resources_destroy(nullptr);
}

}  // namespace
}  // namespace hydroid::res

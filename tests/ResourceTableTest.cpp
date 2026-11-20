// ResourceTable's API, which the aapt2 golden tests see only through its dump: names and ids,
// the values and pools a load fills, references into another package, errors, and the order of
// configurations.

#include <gtest/gtest.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "DecimalCommaLocale.h"
#include "TempDir.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

using test::TempDir;

std::string errorsOf(const std::vector<Diagnostic>& errors) {
  std::ostringstream out;
  for (const Diagnostic& d : errors) out << "\n  " << d;
  return out.str();
}

// Loads `files`, paths under res/ and their text, as the package `name` with `id`.
bool load(ResourceTable& table, const TempDir& dir,
          const std::vector<std::pair<std::string, std::string>>& files,
          const std::string& name = "com.example", uint8_t id = 0x7f,
          std::vector<Diagnostic>* errors = nullptr) {
  const std::filesystem::path res = dir.path() / name;
  for (const auto& [path, text] : files) {
    const std::filesystem::path file = res / std::filesystem::u8path(path);
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
  }
  std::filesystem::create_directories(res);
  return table.addPackage(res, name, id, errors);
}

const Value& valueOf(const ResourceTable& table, const std::string& name) {
  const std::optional<ResId> id = table.find(name);
  EXPECT_TRUE(id) << name;
  static const Value kNone;
  if (!id) return kNone;
  return table.entry(*id)->candidates.at(0).value;
}

TEST(ResType, NamesReadBack) {
  for (int t = 0; t <= static_cast<int>(ResType::Xml); t++) {
    const auto type = static_cast<ResType>(t);
    EXPECT_EQ(parseResType(toString(type)), type) << toString(type);
  }
  EXPECT_EQ(parseResType("styleable"), ResType::Styleable);
  EXPECT_FALSE(parseResType("values"));
  EXPECT_FALSE(parseResType("Drawable"));
  EXPECT_FALSE(parseResType(""));
}

// ResTable_config::compare packs fields into integers, little-endian, so its order is not that of
// a folder name. golden/aapt2/table.txt pins it over many more.
TEST(ConfigOrder, ComparesPackedFields) {
  auto c = [](const char* q) { return *parseConfig(q); };
  EXPECT_EQ(compareConfigs(c(""), c("")), 0);
  EXPECT_EQ(compareConfigs(c("en-rUS"), c("en-rUS")), 0);
  EXPECT_LT(compareConfigs(c(""), c("hdpi")), 0);
  // The mnc is the high half of imsi: mcc311 comes before mcc310-mnc1.
  EXPECT_LT(compareConfigs(c("mcc311"), c("mcc310-mnc1")), 0);
  // The second letter of a language is the high byte: "ja" before "en".
  EXPECT_LT(compareConfigs(c("ja"), c("en")), 0);
  // The region is the high half of the locale.
  EXPECT_LT(compareConfigs(c("ko"), c("en-rUS")), 0);
  // The density is the high half of screenType, the orientation its low byte.
  EXPECT_LT(compareConfigs(c("land"), c("ldpi")), 0);
  // The version comes before the screen layout: ldrtl (no version) before small-v4.
  EXPECT_LT(compareConfigs(c("ldrtl"), c("small")), 0);
}

TEST(ConfigOrder, IsATotalOrder) {
  std::vector<Config> configs;
  for (const char* q :
       {"",     "en",      "en-rUS",   "b+sr+Latn",  "b+es+419", "fil",     "mcc310", "mnc260",
        "hdpi", "land",    "night",    "v21",        "v36.1",    "sw600dp", "ldrtl",  "round",
        "car",  "640x480", "feminine", "keyshidden", "dpad",     "widecg"}) {
    configs.push_back(*parseConfig(q));
  }
  for (const Config& a : configs) {
    for (const Config& b : configs) {
      const int ab = compareConfigs(a, b);
      EXPECT_EQ(ab, -compareConfigs(b, a)) << a << " " << b;
      EXPECT_EQ(ab == 0, a == b) << a << " " << b;
      for (const Config& x : configs) {
        if (ab < 0 && compareConfigs(b, x) < 0) {
          EXPECT_LT(compareConfigs(a, x), 0);
        }
      }
    }
  }
}

TEST(ResourceTable, FindsByName) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><string name='b'>b</string>"
                     "<string name='a'>a</string><color name='c'>#fff</color>"
                     "</resources>"}},
                   "com.example", 0x7f, &errors))
      << errorsOf(errors);
  // Types from 1 in the order of ResType, entries from 0 in the order of their names.
  EXPECT_EQ(table.find("com.example", ResType::Color, "c"), 0x7f010000u);
  EXPECT_EQ(table.find("com.example", ResType::String, "a"), 0x7f020000u);
  EXPECT_EQ(table.find("com.example", ResType::String, "b"), 0x7f020001u);
  EXPECT_EQ(table.find("com.example:string/a"), 0x7f020000u);
  EXPECT_EQ(table.find("@com.example:string/b"), 0x7f020001u);
  EXPECT_EQ(table.find("@string/b", "com.example"), 0x7f020001u);
  EXPECT_EQ(table.find("string/b", "com.example"), 0x7f020001u);
  EXPECT_FALSE(table.find("@string/b"));
  EXPECT_FALSE(table.find("@string/nope", "com.example"));
  EXPECT_FALSE(table.find("@+id/b", "com.example"));
  EXPECT_FALSE(table.find("other:string/b"));
  EXPECT_EQ(table.nameOf(0x7f020001), "com.example:string/b");
  EXPECT_FALSE(table.nameOf(0x7f020002));
  EXPECT_FALSE(table.nameOf(0x7e020001));
  EXPECT_EQ(table.entry(0x7f010000)->name, "c");
  EXPECT_EQ(table.entry(0x7f030000), nullptr);
  ASSERT_NE(table.package(0x7f), nullptr);
  EXPECT_EQ(table.package(0x7f), table.package("com.example"));
  EXPECT_EQ(table.package(0x7f)->type(ResType::String)->id, 2);
  EXPECT_EQ(table.package(0x7f)->type(ResType::Dimen), nullptr);
}

// The ids a <public> gives leave out others, which no lookup finds (#154).
TEST(ResourceTable, FindsAroundTheIdsAPublicGives) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><string name='a'>a</string><string name='b'>b</string>"
                     "<color name='c'>#fff</color>"
                     "<public type='string' name='b' id='0x7f050003'/></resources>"}},
                   "com.example", 0x7f, &errors))
      << errorsOf(errors);
  const Package& p = *table.package(0x7f);
  ASSERT_EQ(p.types.size(), 2u);
  EXPECT_EQ(p.types[0].id, 1);  // color, the lowest type id left
  EXPECT_EQ(p.types[1].id, 5);
  ASSERT_EQ(p.types[1].entries.size(), 2u);
  EXPECT_EQ(p.types[1].entries[0].id, 0);
  EXPECT_EQ(p.types[1].entries[1].id, 3);
  EXPECT_EQ(p.typeWithId(5), p.type(ResType::String));
  EXPECT_EQ(p.typeWithId(2), nullptr);
  EXPECT_EQ(table.find("com.example", ResType::String, "a"), 0x7f050000u);
  EXPECT_EQ(table.find("com.example", ResType::String, "b"), 0x7f050003u);
  EXPECT_EQ(table.entry(0x7f050003)->name, "b");
  EXPECT_EQ(table.nameOf(0x7f050003), "com.example:string/b");
  for (ResId none : {0x7f050001u, 0x7f050002u, 0x7f050004u, 0x7f020000u}) {
    EXPECT_EQ(table.entry(none), nullptr) << std::hex << none;
    EXPECT_FALSE(table.nameOf(none)) << std::hex << none;
  }
}

TEST(ResourceTable, FillsValuesAndPools) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(
      load(table, dir,
           {{"values/v.xml",
             "<resources>"
             "<string name='s1'>same</string><string name='s2'>  same </string>"
             "<string name='styled'>a <b>b</b></string>"
             "<color name='c'>#f80</color><dimen name='d'>16dp</dimen>"
             "<integer name='i'>0x10</integer><bool name='t'>true</bool>"
             "<attr name='mode'><enum name='on' value='1'/><enum name='off' value='0'/></attr>"
             "<attr name='size' format='dimension'/>"
             "<declare-styleable name='View'><attr name='size'/><attr name='mode'/>"
             "</declare-styleable>"
             "<style name='Base'/><style name='Base.Child'><item name='size'>2dp</item>"
             "<item name='mode'>off</item></style>"
             "<array name='a'><item>@color/c</item><item>3</item></array>"
             "<plurals name='p'><item quantity='other'>o</item><item quantity='one'>1</item>"
             "</plurals><id name='plain'/></resources>"},
            {"drawable-hdpi/icon.png", "not decoded"},
            {"values-land/v.xml", "<resources><dimen name='d'>24dp</dimen></resources>"}},
           "com.example", 0x7f, &errors))
      << errorsOf(errors);
  const Package& p = *table.package("com.example");

  const Value& s1 = valueOf(table, "com.example:string/s1");
  const Value& s2 = valueOf(table, "com.example:string/s2");
  ASSERT_EQ(s1.kind(), ValueKind::String);
  EXPECT_EQ(s1, s2);  // the pool holds each string once
  EXPECT_EQ(p.strings[s1.index()], "same");

  const Value& styled = valueOf(table, "com.example:string/styled");
  ASSERT_EQ(styled.kind(), ValueKind::StyledString);
  EXPECT_EQ(p.styledStrings[styled.index()].text, "a b");
  ASSERT_EQ(p.styledStrings[styled.index()].spans.size(), 1u);
  EXPECT_EQ(p.styledStrings[styled.index()].spans[0].name, "b");
  EXPECT_EQ(p.styledStrings[styled.index()].spans[0].firstChar, 2u);
  EXPECT_EQ(p.styledStrings[styled.index()].spans[0].lastChar, 2u);

  EXPECT_EQ(valueOf(table, "com.example:color/c"), Value::color(0xffff8800, ColorFormat::Rgb4));
  EXPECT_EQ(valueOf(table, "com.example:dimen/d"), *parseFloatValue("16dp"));
  EXPECT_EQ(valueOf(table, "com.example:integer/i"), Value::integer(16, IntFormat::Hex));
  EXPECT_EQ(valueOf(table, "com.example:bool/t"), Value::boolean(true));
  EXPECT_EQ(valueOf(table, "com.example:id/plain"), Value::id());

  // One candidate a configuration, in the order of compareConfigs.
  const Entry& d = *table.entry(*table.find("com.example:dimen/d"));
  ASSERT_EQ(d.candidates.size(), 2u);
  EXPECT_EQ(d.candidates[0].config, Config{});
  EXPECT_EQ(d.candidates[1].config, *parseConfig("land"));
  EXPECT_EQ(d.candidates[1].value, *parseFloatValue("24dp"));

  const Value& mode = valueOf(table, "com.example:attr/mode");
  ASSERT_EQ(mode.kind(), ValueKind::AttrDecl);
  const AttrDecl& attr = p.attrs[mode.index()];
  EXPECT_EQ(attr.formats, kFormatEnum);
  ASSERT_EQ(attr.symbols.size(), 2u);  // by name: off, on
  EXPECT_EQ(table.nameOf(attr.symbols[0].id), "com.example:id/off");
  EXPECT_EQ(attr.symbols[0].value, 0u);
  EXPECT_EQ(table.nameOf(attr.symbols[1].id), "com.example:id/on");

  const Value& view = valueOf(table, "com.example:styleable/View");
  ASSERT_EQ(view.kind(), ValueKind::StyleableDecl);
  EXPECT_EQ(p.styleables[view.index()].attrs,
            (std::vector<ResId>{*table.find("com.example:attr/size"),
                                *table.find("com.example:attr/mode")}));

  const Value& child = valueOf(table, "com.example:style/Base.Child");
  ASSERT_EQ(child.kind(), ValueKind::Style);
  const Style& style = p.styles[child.index()];
  EXPECT_EQ(style.parent, *table.find("com.example:style/Base"));
  ASSERT_EQ(style.items.size(), 2u);  // by attribute id: mode, then size
  EXPECT_EQ(style.items[0].attr, *table.find("com.example:attr/mode"));
  EXPECT_EQ(style.items[0].value, Value::integer(0));
  EXPECT_EQ(style.items[1].attr, *table.find("com.example:attr/size"));
  EXPECT_EQ(style.items[1].value, *parseFloatValue("2dp"));

  const Value& a = valueOf(table, "com.example:array/a");
  ASSERT_EQ(a.kind(), ValueKind::Array);
  EXPECT_EQ(p.arrays[a.index()].items,
            (std::vector<Value>{Value::reference(*table.find("com.example:color/c")),
                                Value::integer(3)}));

  const Value& plurals = valueOf(table, "com.example:plurals/p");
  ASSERT_EQ(plurals.kind(), ValueKind::Plurals);
  const Plurals& items = p.plurals[plurals.index()];
  EXPECT_FALSE(items.items[static_cast<size_t>(Quantity::Zero)]);
  ASSERT_TRUE(items.items[static_cast<size_t>(Quantity::One)]);
  EXPECT_EQ(p.strings[items.items[static_cast<size_t>(Quantity::One)]->index()], "1");
  EXPECT_EQ(p.strings[items.items[static_cast<size_t>(Quantity::Other)]->index()], "o");

  const Entry& icon = *table.entry(*table.find("com.example:drawable/icon"));
  ASSERT_EQ(icon.candidates.size(), 1u);
  EXPECT_EQ(icon.candidates[0].config, *parseConfig("hdpi"));
  const FileRef& file = p.files[icon.candidates[0].value.index()];
  EXPECT_EQ(file.path, "res/drawable-hdpi-v4/icon.png");
  EXPECT_EQ(file.type, FileType::Png);

  EXPECT_EQ(describe(p, ResType::Style, child),
            "(style) size=2 parent=style/Base (0x7f0b0000)\nmode(0x7f020000)=0\n"
            "size(0x7f020001)=2.000000dp");
}

// A package may refer to one loaded before it, by its name, as an app does to android: to what
// that package makes <public> (#141).
TEST(ResourceTable, RefersIntoAnotherPackage) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><color name='accent'>#123</color>"
                     "<attr name='mode'><enum name='on' value='5'/></attr>"
                     "<attr name='text' format='string'/><style name='Theme'/>"
                     "<public type='color' name='accent'/><public type='attr' name='mode'/>"
                     "<public type='attr' name='text'/><public type='style' name='Theme'/>"
                     "</resources>"}},
                   "com.lib", 0x7e, &errors))
      << errorsOf(errors);
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources xmlns:lib='http://schemas.android.com/apk/res/com.lib'>"
                     "<color name='c'>@com.lib:color/accent</color>"
                     "<color name='d'>?lib:attr/mode</color>"
                     "<style name='App' parent='com.lib:Theme'>"
                     "<item name='com.lib:mode'>on</item><item name='lib:text'>  a   b </item>"
                     "</style></resources>"}},
                   "com.example", 0x7f, &errors))
      << errorsOf(errors);
  const ResId accent = *table.find("com.lib:color/accent");
  EXPECT_EQ(valueOf(table, "com.example:color/c"), Value::reference(accent));
  EXPECT_EQ(valueOf(table, "com.example:color/d"),
            Value::attrRef(*table.find("com.lib:attr/mode")));
  const Package& app = *table.package("com.example");
  const Style& style = app.styles[valueOf(table, "com.example:style/App").index()];
  EXPECT_EQ(style.parent, *table.find("com.lib:style/Theme"));
  ASSERT_EQ(style.items.size(), 2u);
  EXPECT_EQ(style.items[0].value, Value::integer(5));  // read with com.lib's enum
  EXPECT_EQ(app.strings[style.items[1].value.index()], "a b");

  // As aapt2 dumps an APK, which knows only its own names.
  EXPECT_EQ(describe(app, ResType::Color, valueOf(table, "com.example:color/c")), "@0x7e020000");
  std::ostringstream out;
  dump(out, table);
  EXPECT_NE(out.str().find("Package name=com.lib id=7e\n"), std::string::npos);
  EXPECT_NE(out.str().find("(style) size=2 parent=0x7e040000\n"), std::string::npos) << out.str();
}

// An <attr> with a format in a <declare-styleable> is defined in the package before its name, as
// aapt2 link merges it: the package being loaded keeps it, and another leaves it out, so that the
// styleable refers to that package's attr, or fails to find one (#109). The golden cases load no
// other package.
TEST(ResourceTable, StyleableDefinesAttrsOnlyInItsOwnPackage) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><attr name='a' format='color'/>"
                     "<public type='attr' name='a'/></resources>"}},
                   "com.lib", 0x7e, &errors))
      << errorsOf(errors);
  ASSERT_TRUE(
      load(table, dir,
           {{"values/v.xml",
             "<resources><declare-styleable name='S'>"
             "<attr name='com.lib:a' format='integer'/>"
             "<attr name='com.example:b' format='color'/></declare-styleable></resources>"}},
           "com.example", 0x7f, &errors))
      << errorsOf(errors);
  const Package& app = *table.package("com.example");
  EXPECT_EQ(app.styleables[valueOf(table, "com.example:styleable/S").index()].attrs,
            (std::vector<ResId>{*table.find("com.lib:attr/a"), *table.find("com.example:attr/b")}));
  EXPECT_FALSE(table.find("com.example:attr/a"));

  EXPECT_FALSE(load(table, dir,
                    {{"values/v.xml",
                      "<resources><declare-styleable name='S'>"
                      "<attr name='com.lib:c' format='color'/></declare-styleable></resources>"}},
                    "com.other", 0x7d, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "resource com.lib:attr/c not found");
  EXPECT_FALSE(table.package("com.other"));
}

// aapt2 compile resolves the collisions of a file before link leaves out what it defines in
// another package (#88): two <attr> of another package that disagree fail in one file, as they do
// in this package, and are both left out from two.
TEST(ResourceTable, StyleableDefinesAttrsOfAFileBeforeLeavingOtherPackagesOut) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><attr name='a' format='color'/>"
                     "<public type='attr' name='a'/></resources>"}},
                   "com.lib", 0x7e, &errors))
      << errorsOf(errors);
  const std::string a = "<declare-styleable name='A'><attr name='com.lib:a' format='integer'/>"
                        "</declare-styleable>";
  const std::string b = "<declare-styleable name='B'><attr name='com.lib:a' format='color'/>"
                        "</declare-styleable>";

  EXPECT_FALSE(load(table, dir, {{"values/v.xml", "<resources>" + a + b + "</resources>"}},
                    "com.one", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "duplicate value for resource 'com.lib:attr/a' with config ''");

  errors.clear();
  ASSERT_TRUE(load(table, dir,
                   {{"values/a.xml", "<resources>" + a + "</resources>"},
                    {"values/b.xml", "<resources>" + b + "</resources>"}},
                   "com.two", 0x7f, &errors))
      << errorsOf(errors);
  EXPECT_FALSE(table.find("com.two:attr/a"));
  for (const char* styleable : {"com.two:styleable/A", "com.two:styleable/B"}) {
    const Package& app = *table.package("com.two");
    EXPECT_EQ(app.styleables[valueOf(table, styleable).index()].attrs,
              std::vector<ResId>{*table.find("com.lib:attr/a")})
        << styleable;
  }
}

TEST(ResourceTable, PackageMustBeLoaded) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(
      table, dir,
      {{"values/v.xml", "<resources><color name='c'>@android:color/black</color></resources>"}},
      "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_NE(errors[0].message.find("android"), std::string::npos) << errors[0];
}

TEST(ResourceTable, FailureLeavesTheTableAsItWas) {
  TempDir dir;
  ResourceTable table;
  ASSERT_TRUE(
      load(table, dir, {{"values/v.xml", "<resources><id name='x'/></resources>"}}, "com.a", 0x7e));
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(table, dir,
                    {{"values/v.xml",
                      "<resources><color name='c'>red</color>"
                      "</resources>"}},
                    "com.b", 0x7f, &errors));
  EXPECT_FALSE(errors.empty());
  EXPECT_FALSE(load(table, dir, {}, "com.c", 0x7e));  // the id is taken
  EXPECT_FALSE(load(table, dir, {}, "com.a", 0x7d));  // and the name
  EXPECT_FALSE(load(table, dir, {}, "com.d", 0));
  EXPECT_FALSE(load(table, dir, {}, "", 0x7c));
  ASSERT_EQ(table.packages().size(), 1u);
  EXPECT_EQ(table.packages()[0].name, "com.a");
  EXPECT_FALSE(table.find("com.b:color/c"));
  EXPECT_TRUE(load(table, dir, {}, "com.e", 0x7f));
}

TEST(ResourceTable, ErrorsSayWhere) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(table, dir,
                    {{"values/v.xml",
                      "<resources>\n<string name='a'>a</string>\n"
                      "<integer name='b'>x</integer>\n</resources>\n"}},
                    "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].file.filename(), "v.xml");
  EXPECT_EQ(errors[0].line, 3);
  std::ostringstream out;
  out << errors[0];
  EXPECT_NE(out.str().find("v.xml:3: "), std::string::npos) << out.str();

  errors.clear();
  EXPECT_FALSE(table.addPackage(dir.path() / "missing", "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].line, 0);
  out.str("");
  out << Diagnostic{{}, 0, "plain"};
  EXPECT_EQ(out.str(), "plain");
}

// The tree comes from expat, which gives the line of each element and text, past a DTD over
// several lines and the entities it expands, as aapt2 says (#104).
TEST(ResourceTable, ErrorsSayWherePastADtd) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(table, dir,
                    {{"values/v.xml",
                      "<!DOCTYPE resources [\n<!ENTITY n '1'>\n]>\n<resources>\n"
                      "<integer name='a'>&n;</integer>\n<integer name='b'>&n;x</integer>\n"
                      "<!-- c -->\n\ntext</resources>\n"}},
                    "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 2u);
  EXPECT_EQ(errors[0].line, 6) << errors[0];
  EXPECT_EQ(errors[0].message, "invalid integer");
  EXPECT_EQ(errors[1].line, 9) << errors[1];
  EXPECT_EQ(errors[1].message, "plain text not allowed here");

  // What an entity over several lines holds, element or text, is on the line that refers to it.
  const std::vector<std::pair<std::string, int>> entities = {
      {"<!DOCTYPE resources [\n<!ENTITY s \"<integer name='a'>\n\nx</integer>\">\n]>\n"
       "<resources>\n\n&s;\n</resources>\n",
       8},
      {"<!DOCTYPE resources [\n<!ENTITY s \"\n<foo name='a'>x</foo>\">\n]>\n"
       "<resources>\n\n&s;\n</resources>\n",
       7},
      {"<!DOCTYPE resources [\n<!ENTITY s \"\n\ntext\">\n]>\n"
       "<resources>\n\n&s;\n</resources>\n",
       8},
  };
  int package = 0;
  for (const auto& [xml, line] : entities) {
    errors.clear();
    EXPECT_FALSE(load(table, dir, {{"values/v.xml", xml}}, "com.e" + std::to_string(package++),
                      0x7f, &errors));
    ASSERT_EQ(errors.size(), 1u) << xml;
    EXPECT_EQ(errors[0].line, line) << errors[0];
  }
}

// Plain text between resources is an error for each piece of text expat reports, on its line, as
// XmlPullParser makes a text event of each: a line, an entity or character reference, and the
// text on either side of a comment or processing instruction.
TEST(ResourceTable, PlainTextIsAnErrorForEachPiece) {
  const std::vector<std::pair<std::string, std::vector<int>>> cases = {
      {"<resources>\n\n\ntext</resources>\n", {4}},
      {"<resources>\n a\n b\n</resources>\n", {2, 3}},
      {"<resources>a&amp;b</resources>\n", {1, 1, 1}},
      {"<resources>z<?pi?>w<!-- c -->\nv</resources>\n", {1, 1, 2}},
  };
  TempDir dir;
  int package = 0;
  for (const auto& [xml, lines] : cases) {
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, {{"values/v.xml", xml}}, "com.p" + std::to_string(package++),
                      0x7f, &errors));
    std::vector<int> got;
    for (const Diagnostic& e : errors) {
      EXPECT_EQ(e.message, "plain text not allowed here") << e;
      got.push_back(e.line);
    }
    EXPECT_EQ(got, lines) << xml;
  }
}

// What is not well-formed XML fails with expat's message and the line expat stops at (#78).
// aapt2 gives the same message, but without "xml parser error: " for an XML file, and line 0 for
// a values file, as XmlPullParser keeps no line for the error.
TEST(ResourceTable, XmlErrorsAreExpats) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(table, dir,
                    {{"values/v.xml",
                      "<resources>\n<string name='a'>Tom & Jerry</string>\n"
                      "</resources>\n"}},
                    "com.a", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "xml parser error: not well-formed (invalid token)");
  EXPECT_EQ(errors[0].line, 2);

  // An XML file is read to its end, past the root element.
  errors.clear();
  EXPECT_FALSE(load(table, dir, {{"layout/l.xml", "<a/>\n\n<a/>\n"}}, "com.b", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "xml parser error: junk after document element");
  EXPECT_EQ(errors[0].line, 3);
}

// Text that is not UTF-8 fails the whole file, as aapt2 rejects it, rather than reading as some
// other text (#79).
TEST(ResourceTable, RejectsWhatIsNotUtf8) {
  // A lead byte 0xf8-0xff, C0 and F5-F7, overlong, a surrogate, past U+10FFFF.
  const std::vector<std::string> cases = {"\xf8\x80\x80", "\xc0\xa0",     "\xf5\x80\x80\x80",
                                          "\xe0\x80\xa0", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
  TempDir dir;
  int package = 0;
  for (const std::string& bytes : cases) {
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(
        load(table, dir,
             {{"values/v.xml", "<resources><string name='s'>a" + bytes + "b</string></resources>"}},
             "com.p" + std::to_string(package++), 0x7f, &errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].message, "xml parser error: not well-formed (invalid token)");
  }
}

// An attr named in a style item or a declare-styleable keeps a leading * unless a package follows
// it, and is its whole text when nothing follows the ':', with the package before the ':' kept,
// as ParseXmlAttributeName names it: the error names what aapt2 looks for (#84). The golden cases
// see only that each one fails, and an error leaves out the package being loaded, so android:
// shows that the package is kept.
TEST(ResourceTable, AttrNameIsParseXmlAttributeNames) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"<attr name='n' format='integer'/><style name='S'><item name='*n'>3</item></style>",
       "'attr/*n' not found"},
      {"<attr name='n' format='integer'/><style name='S'><item "
       "name='com.example:'>3</item></style>",
       "'attr/com.example:' not found"},
      {"<style name='S'><item name='android:'>3</item></style>", "android:attr/android:"},
      {"<declare-styleable name='S'><attr name='*a' format='color'/></declare-styleable>",
       "invalid entry name '*a'"},
  };
  for (const auto& [body, expected] : cases) {
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, {{"values/v.xml", "<resources>" + body + "</resources>"}},
                      "com.example", 0x7f, &errors))
        << body;
    ASSERT_EQ(errors.size(), 1u) << body << errorsOf(errors);
    EXPECT_NE(errors[0].message.find(expected), std::string::npos) << errors[0];
  }
}

// The ids of the <enum>s and <flag>s of an <attr> go in after it, as AddResourcesToTable adds a
// resource's children, and not at all if it is not read or its name is not valid, nor is any
// error given of their names then (#156). The golden cases compare only the first error.
TEST(ResourceTable, ChildrenOfAFailedResourceAreLeftOut) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"<attr name='1a'><enum name='2x' value='1'/></attr>", "invalid entry name '1a'"},
      {"<declare-styleable name='s'><attr name='1a'><enum name='2x' value='1'/></attr>"
       "</declare-styleable>",
       "invalid entry name '1a'"},
      {"<declare-styleable name='1s'><attr name='2a' format='string'/></declare-styleable>",
       "invalid entry name '1s'"},
      // More than one child, and grandchildren, all left out (#159).
      {"<attr name='1a'><enum name='2x' value='1'/><enum name='3y' value='2'/></attr>",
       "invalid entry name '1a'"},
      {"<declare-styleable name='1s'><attr name='a'><enum name='2x' value='1'/></attr>"
       "<attr name='3b' format='string'/></declare-styleable>",
       "invalid entry name '1s'"},
      {"<attr name='a'><enum name='2x' value='1'/><enum name='y' value='z'/></attr>",
       "invalid value 'z' for <enum>"},
  };
  for (const auto& [body, expected] : cases) {
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, {{"values/v.xml", "<resources>" + body + "</resources>"}},
                      "com.example", 0x7f, &errors))
        << body;
    ASSERT_EQ(errors.size(), 1u) << body << errorsOf(errors);
    EXPECT_NE(errors[0].message.find(expected), std::string::npos) << errors[0];
  }
}

// Nor are they added when it is a duplicate, which compile finds, and the children of one that
// fails give no errors, though those of the others do (#159). Every error aapt2 37.0.0 compile
// gives of each, in order, but its "resource previously defined here", and with no package where
// it names the one being loaded, which compile does not know (#161). Some that fail have more
// than one child, all of which are left out.
TEST(ResourceTable, ChildrenOfADuplicateAreLeftOut) {
  const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
      {"<attr name='e' format='string'/><attr name='e'><enum name='1x' value='1'/></attr>",
       {"duplicate value for resource 'attr/e' with config ''"}},
      // All the children of one that fails, and their own: none of their names is checked.
      {"<attr name='e' format='string'/><attr name='e'><enum name='1x' value='1'/>"
       "<enum name='2y' value='2'/></attr>",
       {"duplicate value for resource 'attr/e' with config ''"}},
      {"<declare-styleable name='s'/><declare-styleable name='s'><attr name='a'>"
       "<enum name='1x' value='1'/></attr><attr name='1b' format='string'/></declare-styleable>",
       {"duplicate value for resource 'styleable/s' with config ''"}},
      // The attr/b of the styleable that fails is not there for the <attr> after it.
      {"<declare-styleable name='s'><attr name='a' format='string'/></declare-styleable>"
       "<declare-styleable name='s'><attr name='b' format='integer'/></declare-styleable>"
       "<attr name='b' format='string'/>",
       {"duplicate value for resource 'styleable/s' with config ''"}},
      // An <attr> of a styleable that fails, and not the styleable: the next <attr> goes in.
      {"<attr name='a' format='integer'/><declare-styleable name='s'><attr name='a'>"
       "<enum name='1x' value='1'/></attr><attr name='2b' format='string'/></declare-styleable>",
       {"duplicate value for resource 'attr/a' with config ''",
        "resource 'attr/2b' has invalid entry name '2b'"}},
      {"<declare-styleable name='s'><attr name='1a' format='string'/>"
       "<attr name='2b' format='string'/></declare-styleable>",
       {"resource 'attr/1a' has invalid entry name '1a'",
        "resource 'attr/2b' has invalid entry name '2b'"}},
      {"<attr name='e'><enum name='1x' value='1'/><enum name='2y' value='2'/></attr>",
       {"resource 'id/1x' has invalid entry name '1x'",
        "resource 'id/2y' has invalid entry name '2y'"}},
      {"<integer name='i'>1</integer><integer name='i'>2</integer><integer name='1j'>3</integer>",
       {"duplicate value for resource 'integer/i' with config ''",
        "resource 'integer/1j' has invalid entry name '1j'"}},
      // A name that is not valid is given with the package written before it, as a duplicate is,
      // unless that is the one being loaded (#161).
      {"<declare-styleable name='s'><attr name='android:1x' format='string'/>"
       "<attr name='com.example:2y' format='string'/><attr name='com.other:3z' format='string'/>"
       "</declare-styleable>",
       {"resource 'android:attr/1x' has invalid entry name '1x'",
        "resource 'attr/2y' has invalid entry name '2y'",
        "resource 'com.other:attr/3z' has invalid entry name '3z'"}},
      {"<declare-styleable name='s'><attr name='com.example:a' format='string'/>"
       "</declare-styleable><declare-styleable name='t'>"
       "<attr name='com.example:a' format='integer'/></declare-styleable>",
       {"duplicate value for resource 'attr/a' with config ''"}},
  };
  for (const auto& [body, expected] : cases) {
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, {{"values/v.xml", "<resources>" + body + "</resources>"}},
                      "com.example", 0x7f, &errors))
        << body;
    std::vector<std::string> messages;
    for (const Diagnostic& d : errors) messages.push_back(d.message);
    EXPECT_EQ(messages, expected) << body << errorsOf(errors);
  }
}

// The name of a <public>, <java-symbol> or <symbol> is checked as that of a resource with a value,
// where it is written among them, and one that is not valid is not declared (#162). Every error
// aapt2 37.0.0 compile gives of each, in order. aapt2 gives no file or line of those of a symbol
// with no value, which are given where it is written.
TEST(ResourceTable, SymbolNamesAreChecked) {
  const std::vector<std::pair<std::string, std::vector<std::pair<int, std::string>>>> cases = {
      {"<java-symbol type='string' name='1x'/><string name='s'>a</string>",
       {{1, "resource 'string/1x' has invalid entry name '1x'"}}},
      {"<public type='string' name='1x'/>\n<string name='1x'>a</string>",
       {{1, "resource 'string/1x' has invalid entry name '1x'"},
        {2, "resource 'string/1x' has invalid entry name '1x'"}}},
      {"<string name='1s'>a</string>\n<symbol type='string' name='1x'/>\n"
       "<symbol type='string' name='2y'/>",
       {{1, "resource 'string/1s' has invalid entry name '1s'"},
        {2, "resource 'string/1x' has invalid entry name '1x'"},
        {3, "resource 'string/2y' has invalid entry name '2y'"}}},
      // Before its id is looked at.
      {"<public type='string' name='1x' id='0x7f010000'/>"
       "<public type='string' name='1x' id='0x7f010001'/>",
       {{1, "resource 'string/1x' has invalid entry name '1x'"},
        {1, "resource 'string/1x' has invalid entry name '1x'"}}},
      // A <public type="id"> is its id too, which aapt2 adds with it: one error of each, which
      // aapt2 gives on its line.
      {"<public type='id' name='1x'/>", {{1, "resource 'id/1x' has invalid entry name '1x'"}}},
      {"<public type='id' name='1x'/>\n<java-symbol type='id' name='1x'/>",
       {{1, "resource 'id/1x' has invalid entry name '1x'"},
        {2, "resource 'id/1x' has invalid entry name '1x'"}}},
      {"<public type='id' name='1x' id='0x7f010000'/>\n"
       "<public type='id' name='1x' id='0x7f010001'/>",
       {{1, "resource 'id/1x' has invalid entry name '1x'"},
        {2, "resource 'id/1x' has invalid entry name '1x'"}}},
      {"<public type='id' name='1x'/>\n<id name='1x'/>",
       {{1, "resource 'id/1x' has invalid entry name '1x'"},
        {2, "resource 'id/1x' has invalid entry name '1x'"}}},
  };
  for (const auto& [body, expected] : cases) {
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, {{"values/v.xml", "<resources>" + body + "</resources>"}},
                      "com.example", 0x7f, &errors))
        << body;
    std::vector<std::pair<int, std::string>> got;
    for (const Diagnostic& d : errors) {
      got.emplace_back(d.line, d.message);
      EXPECT_EQ(d.file, dir.path() / "com.example" / "values" / "v.xml") << d;
    }
    EXPECT_EQ(got, expected) << body << errorsOf(errors);
  }
}

// A file aapt2 fails with "invalid file path 'res/...'" fails with what is wrong with its path,
// on the file. Aapt2Table.CasesLikeAapt2 compares only aapt2's words, "invalid file path".
TEST(ResourceTable, InvalidFilePathSaysWhy) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"foo/x.png", "invalid file path: no resource type 'foo'"},
      {"Raw/x.txt", "invalid file path: no resource type 'Raw'"},
      {"values/a.txt", "invalid file path: not an .xml file in values"},
  };
  for (const auto& [path, expected] : cases) {
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, {{path, "x"}}, "com.example", 0x7f, &errors)) << path;
    ASSERT_EQ(errors.size(), 1u) << path << errorsOf(errors);
    EXPECT_EQ(errors[0].message, expected);
    EXPECT_EQ(errors[0].file, dir.path() / "com.example" / std::filesystem::u8path(path));
    EXPECT_EQ(errors[0].line, 0);
  }
}

// Keeps everyone from listing `dir` while it lives, and lets them again after: by an ACL on
// Windows, where permissions() only sets the read-only flag, and by its mode elsewhere.
class Unlistable {
 public:
  explicit Unlistable(std::filesystem::path dir) : dir_(std::move(dir)) {
#ifdef _WIN32
    icacls("/deny *S-1-1-0:(RX)");  // S-1-1-0 is Everyone
#else
    std::filesystem::permissions(dir_, std::filesystem::perms::none);
#endif
  }
  ~Unlistable() {
#ifdef _WIN32
    icacls("/remove:d *S-1-1-0");
#else
    std::error_code ec;
    std::filesystem::permissions(dir_, std::filesystem::perms::owner_all, ec);
#endif
  }
  // Whether it took: a user who may list any folder, as root, still lists this one.
  bool works() const {
    std::error_code ec;
    std::filesystem::directory_iterator(dir_, ec);
    return static_cast<bool>(ec);
  }

 private:
#ifdef _WIN32
  void icacls(const std::string& args) const {
    std::system(("icacls \"" + dir_.string() + "\" " + args + " >nul").c_str());
  }
#endif
  std::filesystem::path dir_;
};

// A folder of res/ that cannot be opened fails the load, rather than leaving its values out as
// aapt2 37.0.0 on Windows does without a word (#85).
TEST(ResourceTable, FolderThatCannotBeOpenedFails) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  for (const char* folder : {"values", "values-fr"}) {
    std::filesystem::create_directories(res / folder);
    std::ofstream(res / folder / "v.xml") << "<resources><string name='s'>x</string></resources>";
  }
  const Unlistable fr(res / "values-fr");  // after the TempDir, so that it goes first
  if (!fr.works()) GTEST_SKIP() << "this user may list a folder that denies it";
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(res, "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "failed to open directory");
  EXPECT_EQ(errors[0].file.filename(), "values-fr");
}

// Makes `link` a link to the folder `target`: a junction on Windows, which needs no privilege as
// a symbolic link does there, and a symbolic link elsewhere.
bool linkFolder(const std::filesystem::path& link, const std::filesystem::path& target) {
#ifdef _WIN32
  const std::string mklink = "mklink /J \"" + link.string() + "\" \"" + target.string() + "\" >nul";
  return std::system(mklink.c_str()) == 0;
#else
  std::error_code ec;
  std::filesystem::create_directory_symlink(target, link, ec);
  return !ec;
#endif
}

// So does a link to a folder that cannot be opened, whose kind cannot be told on Windows, and
// which fails to open elsewhere.
TEST(ResourceTable, FolderLinkThatCannotBeOpenedFails) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  const std::filesystem::path target = dir.path() / "fr";
  std::filesystem::create_directories(res / "values");
  std::filesystem::create_directories(target);
  std::ofstream(res / "values" / "v.xml") << "<resources><string name='s'>x</string></resources>";
  std::ofstream(target / "v.xml") << "<resources><string name='s'>y</string></resources>";
  ASSERT_TRUE(linkFolder(res / "values-fr", target));
  const Unlistable fr(target);
  if (!fr.works()) GTEST_SKIP() << "this user may list a folder that denies it";
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(res, "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "failed to open directory");
  EXPECT_EQ(errors[0].file.filename(), "values-fr");
}

// But a link to nothing is left out, as aapt2 leaves it: nothing is behind it to lack.
TEST(ResourceTable, FolderLinkToNothingIsLeftOut) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  const std::filesystem::path target = dir.path() / "fr";
  std::filesystem::create_directories(res / "values");
  std::filesystem::create_directories(target);
  std::ofstream(res / "values" / "v.xml") << "<resources><string name='s'>x</string></resources>";
  ASSERT_TRUE(linkFolder(res / "values-fr", target));
  std::filesystem::remove(target);
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_TRUE(table.addPackage(res, "com.example", 0x7f, &errors)) << errorsOf(errors);
  EXPECT_TRUE(table.find("com.example:string/s"));
}

// Loads `res` and expects it to fail on res/raw/data.bin alone, which cannot be opened (#86).
void expectDataBinFails(const std::filesystem::path& res) {
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(res, "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "failed to open file");
  EXPECT_EQ(errors[0].file.filename(), "data.bin");
}

// In a type folder, though, a link to a file that is not there fails the load, as aapt2 fails to
// open it, rather than leaving raw/data.bin out (#86). Windows lets only a privileged user make
// the link.
TEST(ResourceTable, FileLinkToNothingFails) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  std::filesystem::create_directories(res / "raw");
  std::ofstream(res / "raw" / "ok.bin") << "ok";
  std::error_code ec;
  std::filesystem::create_symlink(dir.path() / "gone.bin", res / "raw" / "data.bin", ec);
  if (ec) GTEST_SKIP() << "cannot make a symbolic link: " << ec.message();
  expectDataBinFails(res);
}

// So does a link there to a folder that is not there, which cannot be told from a link to a file
// elsewhere than Windows. aapt2 on Windows leaves it out, taking it by its attributes for a folder,
// which hydroid does not, so that a load never quietly lacks what such a name meant to bring.
TEST(ResourceTable, FolderLinkToNothingInATypeFolderFails) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  const std::filesystem::path target = dir.path() / "gone";
  std::filesystem::create_directories(res / "raw" / "sub");  // a folder is still left out
  std::filesystem::create_directories(target);
  ASSERT_TRUE(linkFolder(res / "raw" / "data.bin", target));
  std::filesystem::remove(target);
  expectDataBinFails(res);
}

#ifndef _WIN32
// A pipe is not opened, which would wait until something wrote to it.
TEST(ResourceTable, PipeFails) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  std::filesystem::create_directories(res / "raw");
  ASSERT_EQ(mkfifo((res / "raw" / "data.bin").c_str(), 0600), 0);
  expectDataBinFails(res);
}
#endif

#ifdef _WIN32
// A file that opens but cannot be read, as another holds a lock on its bytes, fails the load
// rather than being taken for an empty file.
TEST(ResourceTable, FileThatCannotBeReadFails) {
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  const std::filesystem::path file = res / "raw" / "data.bin";
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file) << "data";
  struct Locked {
    HANDLE handle;
    OVERLAPPED at{};
    ~Locked() {
      UnlockFileEx(handle, 0, 1, 0, &at);
      CloseHandle(handle);
    }
  };
  const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(handle, INVALID_HANDLE_VALUE);
  Locked locked{handle};  // after the TempDir, so that it goes first
  ASSERT_TRUE(
      LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &locked.at));
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(res, "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "failed to read file");
  EXPECT_EQ(errors[0].file.filename(), "data.bin");
}
#endif

#ifdef __linux__
// So does one on Linux, here a link to /proc/self/mem: a regular file that opens, whose byte 0,
// unmapped, cannot be read. Through an ifstream, libstdc++ threw out of addPackage on it.
TEST(ResourceTable, FileThatCannotBeReadFailsOnLinux) {
  {
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> mem(std::fopen("/proc/self/mem", "rb"),
                                                        std::fclose);
    char byte;
    if (!mem || std::fread(&byte, 1, 1, mem.get()) != 0 || !std::ferror(mem.get())) {
      GTEST_SKIP() << "/proc/self/mem does not fail to read here";
    }
  }
  TempDir dir;
  const std::filesystem::path res = dir.path() / "res";
  std::filesystem::create_directories(res / "raw");
  std::filesystem::create_symlink("/proc/self/mem", res / "raw" / "data.bin");
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(res, "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "failed to read file");
  EXPECT_EQ(errors[0].file.filename(), "data.bin");
}
#endif

// A '.' in the name of a file that is compiled fails the load with aapt2's message, one for each
// such file, while a file that is copied as it is may have one (#121).
TEST(ResourceTable, DotInTheNameOfACompiledFileFails) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(table, dir,
                    {{"values/a.b.xml", "<resources/>"},
                     {"layout/a.b.xml", "<a/>"},
                     {"drawable/n.b.9.png", "x"},
                     {"drawable/a.b.jpg", "x"},
                     {"raw/a.b.txt", "x"}},
                    "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 3u) << errorsOf(errors);
  for (const Diagnostic& e : errors) {
    EXPECT_EQ(e.message, "file name cannot contain '.' other than for specifying the extension");
  }
  EXPECT_EQ(errors[0].file.filename(), "n.b.9.png");
  EXPECT_EQ(errors[1].file.filename(), "a.b.xml");
  EXPECT_EQ(errors[2].file.filename(), "a.b.xml");
}

// Two files that aapt2 compiles to one name, as a folder that ends in '-' and the folder without
// it give, fail the load, on the second of them by folder/name (#122).
TEST(ResourceTable, FilesOfOneCompiledNameFail) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(load(table, dir,
                    {{"values-/a.xml", "<resources><string name='s'>s</string></resources>"},
                     {"values/a.xml", "<resources><string name='t'>t</string></resources>"},
                     {"raw-/x.txt", "x"},
                     {"raw/x.txt", "y"},
                     {"raw/y.txt", "y"}},
                    "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 2u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message,
            "Duplicate entries in archive: compiles to raw_x.txt.flat, as raw-/x.txt does");
  EXPECT_EQ(errors[1].message,
            "Duplicate entries in archive: compiles to values_a.arsc.flat, as values-/a.xml does");
}

// aapt2 compiles both files of one compiled name, and link, which fails on them, only runs if
// compile gave no error (#158): compile's errors, of the second file too, come alone, and the one
// of the name comes before any other of link.
TEST(ResourceTable, FilesOfOneCompiledNameFailAtLink) {
  const std::string s = "<resources><string name='s'>s</string></resources>";
  const std::string duplicate =
      "Duplicate entries in archive: compiles to values_a.arsc.flat, as values-/a.xml does";
  using Files = std::vector<std::pair<std::string, std::string>>;
  const std::vector<std::pair<Files, std::vector<std::string>>> cases = {
      {{{"values-/a.xml", s},
        {"values/a.xml", s},
        {"values/b.xml",
         "<resources><string name='q'>1</string><string name='q'>2</string></resources>"}},
       {"duplicate value for resource 'string/q' with config ''"}},
      {{{"values-/a.xml", s},
        {"values/a.xml", "<resources><attr name='a' format='colour'/></resources>"}},
       {"invalid attribute format 'colour'"}},
      {{{"layout-/l.xml", "<a/>"}, {"layout/l.xml", "<a"}}, {"xml parser error"}},
      // Not the value that the two files of one name give, nor a reference that is not found, nor
      // a symbol of no value.
      {{{"values-/a.xml", "<resources><string name='s'>@string/missing</string></resources>"},
        {"values/a.xml",
         "<resources><string name='s'>t</string><public type='string' name='nope'/></resources>"}},
       {duplicate}},
  };
  for (const auto& [files, expected] : cases) {
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, files, "com.example", 0x7f, &errors)) << files.back().second;
    ASSERT_EQ(errors.size(), expected.size()) << files.back().second << errorsOf(errors);
    for (size_t i = 0; i < expected.size(); i++) {
      EXPECT_EQ(errors[i].message.substr(0, expected[i].size()), expected[i]) << errors[i];
    }
  }
}

// A folder of no configuration and an @+id/ of no valid name fail with aapt2's words, then what
// hydroid adds (#149). aapt2 names the qualifiers, all that follows the first '-', and the id as
// written, its package too, which hydroid leaves out where it is the one being loaded, as the
// golden cases take it out of aapt2's words (#172). CasesLikeAapt2 sees only that hydroid's words
// take in aapt2's, not the folder name after them.
TEST(ResourceTable, BadFolderAndIdNamesSayWhatAapt2Says) {
  using Files = std::vector<std::pair<std::string, std::string>>;
  const std::vector<std::pair<Files, std::string>> cases = {
      {{{"raw-land-hdpi-port/x.txt", "x"}},
       "invalid configuration 'land-hdpi-port' in folder name 'raw-land-hdpi-port'"},
      {{{"values-foo-bar/v.xml", "<resources/>"}},
       "invalid configuration 'foo-bar' in folder name 'values-foo-bar'"},
      {{{"layout/l.xml",
         "<a xmlns:android='http://schemas.android.com/apk/res/android' "
         "android:id='@+id/1bad'/>"}},
       "id 'id/1bad' has an invalid entry name"},
      {{{"layout/l.xml",
         "<a xmlns:android='http://schemas.android.com/apk/res/android' "
         "android:id='@+com.foo:id/1bad'/>"}},
       "id 'com.foo:id/1bad' has an invalid entry name"},
      {{{"layout/l.xml",
         "<a xmlns:android='http://schemas.android.com/apk/res/android' "
         "android:id='@+com.example:id/1bad'/>"}},
       "id 'id/1bad' has an invalid entry name"},
      {{{"layout/l.xml",
         "<a xmlns:android='http://schemas.android.com/apk/res/android' "
         "android:id='@+android:id/1bad'/>"}},
       "id 'android:id/1bad' has an invalid entry name"},
  };
  for (const auto& [files, message] : cases) {
    SCOPED_TRACE(message);
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(table, dir, files, "com.example", 0x7f, &errors));
    ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
    EXPECT_EQ(errors[0].message, message);
  }
}

// An id @+pkg:id/ makes in an XML file is the package's own when pkg is its name, fails the load
// in another package, and is left out in android, which aapt2 only warns of (#83).
TEST(ResourceTable, IdOfAnotherPackageInAnXmlFile) {
  TempDir dir;
  ResourceTable table;
  ASSERT_TRUE(load(table, dir,
                   {{"layout/l.xml",
                     "<a xmlns:android='http://schemas.android.com/apk/res/android' "
                     "android:id='@+com.example:id/own' x='@+android:id/text1'/>"}}));
  EXPECT_TRUE(table.find("com.example:id/own"));
  EXPECT_FALSE(table.find("com.example:id/text1"));

  // @+id/b before @+app:id/b, which is another id; an error for each id of another package, not
  // for each file that makes it, from the first that does, in the order of their packages, as
  // bytes ('A' before 'a'), then of their names.
  TempDir other;
  ResourceTable failed;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(
      load(failed, other,
           {{"layout/l.xml", "<a\n x='@+id/b' y='@+app:id/b' z='@+app:id/a' w='@+Ab:id/b'/>"},
            {"layout/m.xml", "<a y='@+app:id/b'/>"}},
           "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 3u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "defined resource 'Ab:id/b' for external package 'Ab'");
  EXPECT_EQ(errors[1].message, "defined resource 'app:id/a' for external package 'app'");
  EXPECT_EQ(errors[2].message, "defined resource 'app:id/b' for external package 'app'");
  EXPECT_EQ(errors[2].file.filename(), "l.xml");
  EXPECT_EQ(errors[2].line, 1);
}

// An id @+pkg:id/ makes in a values file is the package's own when pkg, as written, is its name.
// In another package it is left out with no error of its own, unlike one an XML file makes, and
// the reference to it then fails (#131).
TEST(ResourceTable, IdOfAPackageInAValuesFile) {
  TempDir dir;
  ResourceTable table;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><string name='s'>@+com.example:id/own</string>"
                     "<string-array name='a'><item>@+com.example:id/in_array</item></string-array>"
                     "</resources>"}}));
  EXPECT_TRUE(table.find("com.example:id/own"));
  EXPECT_TRUE(table.find("com.example:id/in_array"));

  TempDir other;
  ResourceTable failed;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(
      load(failed, other,
           {{"values/v.xml", "<resources><string name='s'>@+app:id/v</string></resources>"}},
           "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "resource app:id/v not found: no package 'app' is loaded");

  // x of xmlns:x names the package itself in the reference, but not in the id it makes.
  TempDir prefix;
  ResourceTable prefixed;
  errors.clear();
  EXPECT_FALSE(load(prefixed, prefix,
                    {{"values/v.xml",
                      "<resources xmlns:x='http://schemas.android.com/apk/res/com.example'>"
                      "<string name='s'>@+x:id/v</string></resources>"}},
                    "com.example", 0x7f, &errors));
  ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
  EXPECT_EQ(errors[0].message, "resource id/v not found");
}

// A reference of a type aapt2 has and no ResType stands for, as macro, names nothing, though an
// anim, the ResType a ResName holds by default, has the name, in the package itself or in another
// (#142), in a value, a style item and an attribute of an XML file. aapt2 says so of a macro in
// its own words, also in a package not loaded. The golden cases load no other package, have no
// anim of the name and do not compare errors.
TEST(ResourceTable, ReferenceOfATypeWithNoResTypeIsNotFound) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir, {{"anim/a.xml", "<set/>"}}, "com.lib", 0x7e, &errors))
      << errorsOf(errors);
  const std::pair<const char*, const char*> cases[] = {
      {"@macro/a", "failed to find definition for macro/a"},
      {"@configVarying/a", "resource configVarying/a not found"},
      {"@^attr-private/a", "resource ^attr-private/a not found"},
      {"@com.lib:macro/a", "failed to find definition for com.lib:macro/a"},
      {"@*com.lib:configVarying/a", "resource com.lib:configVarying/a not found"},
      {"@com.lib:^attr-private/a", "resource com.lib:^attr-private/a not found"},
      {"@com.none:macro/a", "failed to find definition for com.none:macro/a"},
      {"@com.none:configVarying/a",
       "resource com.none:configVarying/a not found: no package 'com.none' is loaded"},
  };
  for (const auto& [written, expected] : cases) {
    const std::string ref = written;
    const std::string values = "<resources><attr name='t' format='string'/>";
    for (const auto& [v, layout] : std::vector<std::pair<std::string, std::string>>{
             {values + "<string name='s'>" + ref + "</string></resources>", "<a/>"},
             {values + "<style name='S'><item name='t'>" + ref + "</item></style></resources>",
              "<a/>"},
             {values + "</resources>", "<a x='" + ref + "'/>"}}) {
      errors.clear();
      EXPECT_FALSE(load(table, dir,
                        {{"anim/a.xml", "<set/>"}, {"values/v.xml", v}, {"layout/l.xml", layout}},
                        "com.example", 0x7f, &errors))
          << v << layout;
      ASSERT_EQ(errors.size(), 1u) << v << layout << errorsOf(errors);
      EXPECT_EQ(errors[0].message, expected) << v << layout;
    }
  }
  EXPECT_FALSE(table.find("com.lib:macro/a"));
  EXPECT_FALSE(table.find("@configVarying/a", "com.lib"));
  EXPECT_TRUE(table.find("@anim/a", "com.lib"));
}

// What another package does not make <public> only a private reference names: @*, * before a
// style parent or an attribute's name, or a prefix of an .../apk/prv/res/ namespace. Any other
// fails as private, in a values file and an XML file alike, as aapt2 37.0.0 links each case
// against com.other's apk (#141). Its own resources a package names freely. The golden cases load
// no other package.
TEST(ResourceTable, AnotherPackagesPrivateResourceNeedsAPrivateReference) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><string name='pub'>p</string><string name='priv'>q</string>"
                     "<attr name='pa' format='string'/><attr name='qa' format='string'/>"
                     "<style name='PS'/><style name='QS'/>"
                     "<string name='own'>@string/priv</string>"
                     "<public type='string' name='pub'/><public type='attr' name='pa'/>"
                     "<public type='style' name='PS'/></resources>"}},
                   "com.other", 0x7e, &errors))
      << errorsOf(errors);

  const std::string ns =
      " xmlns:o='http://schemas.android.com/apk/res/com.other'"
      " xmlns:q='http://schemas.android.com/apk/prv/res/com.other'";
  // A values file's resources, or an XML file's attributes, and the error, if any.
  const std::vector<std::tuple<std::string, std::string, std::string>> cases = {
      {"<string name='s'>@com.other:string/priv</string>", "",
       "resource com.other:string/priv is private"},
      {"<string name='s'>?com.other:attr/qa</string>", "",
       "resource com.other:attr/qa is private"},
      {"<style name='S' parent='@com.other:style/QS'/>", "",
       "resource com.other:style/QS is private"},
      {"<style name='S'><item name='com.other:qa'>x</item></style>", "",
       "style attribute 'com.other:attr/qa' is private"},
      {"<declare-styleable name='D'><attr name='com.other:qa'/></declare-styleable>", "",
       "resource com.other:attr/qa is private"},
      {"", "x='@com.other:string/priv'", "resource com.other:string/priv is private"},
      {"", "x='?com.other:attr/qa'", "resource com.other:attr/qa is private"},
      {"", "o:qa='1'", "attribute com.other:qa is private"},
      {"<string name='s'>@*com.other:string/priv</string>", "", ""},
      {"<string name='s'>@q:string/priv</string>", "", ""},
      {"<string name='s'>@o:string/pub</string>", "", ""},
      {"<style name='S' parent='@*com.other:style/QS'/>", "", ""},
      {"<style name='S' parent='*com.other:style/QS'/>", "", ""},
      {"<style name='S' parent='*o:QS'/>", "", ""},
      {"<style name='S' parent='com.other:QS'/>", "", "resource com.other:style/QS is private"},
      {"<style name='S' parent='@q:style/QS'/>", "", ""},
      {"<style name='S' parent='@com.other:style/PS'/>", "", ""},
      {"<style name='S'><item name='*com.other:qa'>x</item></style>", "", ""},
      {"<style name='S'><item name='q:qa'>x</item><item name='o:pa'>x</item></style>", "", ""},
      // A public prefix leaves @* and * private.
      {"<string name='s'>@*o:string/priv</string>", "", ""},
      {"<style name='S' parent='@*o:style/QS'/>", "", ""},
      {"<style name='S'><item name='*o:qa'>x</item></style>", "", ""},
      {"<declare-styleable name='D'><attr name='*o:qa'/></declare-styleable>", "", ""},
      {"", "x='@*o:string/priv'", ""},
      {"<declare-styleable name='D'><attr name='*com.other:qa'/></declare-styleable>", "", ""},
      {"", "x='@*com.other:string/priv' y='@q:string/priv' z='?q:attr/qa' w='@o:string/pub'", ""},
      {"", "q:qa='1' o:pa='1'", ""},
  };
  uint8_t id = 0x7d;
  for (const auto& [values, attributes, message] : cases) {
    SCOPED_TRACE(values + attributes);
    errors.clear();
    const bool loaded =
        load(table, dir,
             {{"values/v.xml", "<resources" + ns + ">" + values + "</resources>"},
              {"layout/l.xml", "<a" + ns + " " + attributes + "/>"}},
             "com.probe" + std::to_string(id), id, &errors);
    id--;
    if (message.empty()) {
      EXPECT_TRUE(loaded) << errorsOf(errors);
      continue;
    }
    EXPECT_FALSE(loaded);
    ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
    EXPECT_EQ(errors[0].message, message);
  }
}

// A resource of type attr that is not an <attr>, as <item type='attr' name='r'> in values/, is one
// aapt2 37.0.0 does not find where a style item or an XML attribute names an <attr> (#166), nor by
// any reference (#175), in its own package and in another alike, <public> or not, as it links
// against com.other's apk. The golden cases have the own package's, with no other loaded, and its
// one with no value of the default configuration, as in values-land/ only, which a reference finds
// and a style item or an XML attribute is told is not an attribute.
TEST(ResourceTable, AttrItemIsNotFoundAsAnAttr) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  const std::string item = "<string name='s'>x</string><item type='attr' name='r'>@string/s</item>";
  const std::string other = "<resources>" + item +
                            "<item type='attr' name='p'>@string/s</item>"
                            "<public type='attr' name='r'/></resources>";
  ASSERT_TRUE(load(table, dir, {{"values/v.xml", other}}, "com.other", 0x7e, &errors))
      << errorsOf(errors);
  const std::string ns =
      " xmlns:app='http://schemas.android.com/apk/res-auto'"
      " xmlns:o='http://schemas.android.com/apk/res/com.other'";
  // A values file's resources, or an XML file's attributes, and the error. A values file that
  // fails to link stops link before the XML files (#165), so each case has one or the other.
  const std::vector<std::tuple<std::string, std::string, std::string>> cases = {
      {"<style name='S'><item name='r'>1</item></style>", "", "style attribute 'attr/r' not found"},
      {"<style name='S'><item name='com.other:r'>1</item></style>", "",
       "style attribute 'com.other:attr/r' not found"},
      {"", "app:r='1'", "attribute r not found"},
      {"", "o:r='1'", "attribute com.other:r not found"},
      {"<style name='S'><item name='com.other:p'>1</item></style>", "",
       "style attribute 'com.other:attr/p' not found"},
      {"", "o:p='1'", "attribute com.other:p not found"},
      {"<item type='string' name='t'>?com.other:attr/r</item>", "",
       "resource com.other:attr/r not found"},
      {"<item type='string' name='t'>@com.other:attr/r</item>", "",
       "resource com.other:attr/r not found"},
      {"<item type='string' name='t'>?com.other:attr/p</item>", "",
       "resource com.other:attr/p not found"},
      {"", "x='?o:attr/r'", "resource com.other:attr/r not found"},
  };
  uint8_t id = 0x7d;
  for (const auto& [values, attributes, message] : cases) {
    SCOPED_TRACE(values + attributes);
    errors.clear();
    EXPECT_FALSE(load(table, dir,
                      {{"values/v.xml", "<resources" + ns + ">" + item + values + "</resources>"},
                       {"layout/l.xml", "<a" + ns + " " + attributes + "/>"}},
                      "com.probe" + std::to_string(id), id, &errors));
    id--;
    ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
    EXPECT_EQ(errors[0].message, message);
  }
}

// What an XML file names in android is linked once a package android is loaded, as aapt2 links
// it against android.jar, and left alone while none is, as hydroid cannot read android.jar yet
// (#132). The golden cases load no android, so the cases that need one are here.
TEST(ResourceTable, XmlFileLinksIntoAndroidOnceLoaded) {
  const std::string head =
      "<?xml version='1.0'?>\n<a xmlns:android='http://schemas.android.com/apk/res/android' "
      "xmlns:p='http://schemas.android.com/apk/prv/res/android'\n";
  const std::vector<std::string> intoAndroid = {
      "android:id='@+android:id/gen'", "android:nosuch='1'",         "android:layout_width='foo'",
      "p:layout_width='foo'",          "x='@android:string/nosuch'", "x='?android:attr/nosuch'",
      "x='@*android:string/nosuch'",
  };
  for (const std::string& attribute : intoAndroid) {
    SCOPED_TRACE(attribute);
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_TRUE(
        load(table, dir, {{"layout/l.xml", head + attribute + "/>"}}, "com.example", 0x7f, &errors))
        << errorsOf(errors);
  }

  // Public, as what another package names in android must be for aapt2.
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><attr name='layout_width' format='dimension'>"
                     "<enum name='match_parent' value='-1'/></attr><attr name='text'/>"
                     "<attr name='id' format='reference'/>"
                     "<item type='id' name='text1'/><string name='ok'>ok</string>"
                     "<public type='attr' name='layout_width'/><public type='attr' name='text'/>"
                     "<public type='attr' name='id'/><public type='id' name='text1'/>"
                     "<public type='string' name='ok'/></resources>"}},
                   "android", 0x01, &errors))
      << errorsOf(errors);
  ASSERT_TRUE(load(table, dir,
                   {{"layout/l.xml",
                     head + "android:id='@+android:id/text1' android:layout_width='match_parent' "
                            "android:text='@android:string/ok' p:layout_width=' 10dp '/>"}},
                   "com.example", 0x7f, &errors))
      << errorsOf(errors);
  const std::vector<std::pair<std::string, std::string>> rejected = {
      {intoAndroid[0], "resource android:id/gen not found"},
      {intoAndroid[1], "attribute android:nosuch not found"},
      // With no package, as aapt2 names it, where "not found" gives one (#167).
      {intoAndroid[2],
       "'foo' is incompatible with attribute layout_width (attr) dimension|enum "
       "[match_parent=4294967295]"},
      {intoAndroid[3],
       "'foo' is incompatible with attribute layout_width (attr) dimension|enum "
       "[match_parent=4294967295]"},
      {intoAndroid[4], "resource android:string/nosuch not found"},
      {intoAndroid[5], "resource android:attr/nosuch not found"},
      {intoAndroid[6], "resource android:string/nosuch not found"},
  };
  uint8_t id = 0x7e;
  for (const auto& [attribute, message] : rejected) {
    SCOPED_TRACE(attribute);
    errors.clear();
    EXPECT_FALSE(load(table, dir, {{"layout/l.xml", head + attribute + "/>"}},
                      "com.example" + std::to_string(id), id, &errors));
    ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
    EXPECT_EQ(errors[0].message, message);
    EXPECT_EQ(errors[0].file.filename(), "l.xml");
    EXPECT_EQ(errors[0].line, 2);  // of the element, not of the attribute, as aapt2 gives it
    id--;
  }
}

// An XML attribute's value the <attr> does not take names the <attr> as aapt2's Attribute::Print
// does: its symbols in the order of their names, each value a uint32_t, then min and max, and no
// [weak] for one of a <declare-styleable> (#149). The golden cases have one symbol at most, and
// no min or max; these are what aapt2 37.0.0 says.
TEST(ResourceTable, IncompatibleXmlAttributeNamesTheAttrAsAapt2Does) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"<attr name='foo'><enum name='y' value='2'/><enum name='x' value='-1'/></attr>",
       "(attr) enum [x=4294967295, y=2]"},
      {"<attr name='foo'><flag name='y' value='0x2'/><flag name='x' value='0x1'/></attr>",
       "(attr) flags [x=1, y=2]"},
      {"<attr name='foo' format='integer' min='0' max='3'/>", "(attr) integer min=0 max=3"},
      {"<declare-styleable name='S'><attr name='foo' format='integer'/></declare-styleable>",
       "(attr) integer"},
  };
  for (const auto& [attr, printed] : cases) {
    SCOPED_TRACE(attr);
    TempDir dir;
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(load(
        table, dir,
        {{"values/v.xml", "<resources>" + attr + "</resources>"},
         {"layout/l.xml", "<a xmlns:app='http://schemas.android.com/apk/res-auto' app:foo='z'/>"}},
        "com.example", 0x7f, &errors));
    ASSERT_EQ(errors.size(), 1u) << errorsOf(errors);
    EXPECT_EQ(errors[0].message, "'z' is incompatible with attribute foo " + printed);
  }
}

// Link reads each <attr> once, of this package or another, for all the style items and XML
// attributes that name it (#92): each reads it as the first did, an <attr> of the name of another
// package's as its own, and each that gives what the <attr> does not take fails on its own.
TEST(ResourceTable, EachItemReadsTheAttrItNames) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><attr name='mode'><enum name='on' value='5'/></attr>"
                     "<public type='attr' name='mode'/></resources>"}},
                   "com.lib", 0x7e, &errors))
      << errorsOf(errors);
  const std::string layout =
      "<a xmlns:app='http://schemas.android.com/apk/res-auto' "
      "xmlns:lib='http://schemas.android.com/apk/res/com.lib' ";
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><attr name='mode'><enum name='on' value='7'/></attr>"
                     "<attr name='size'><flag name='a' value='1'/><flag name='b' value='2'/></attr>"
                     "<style name='A'><item name='com.lib:mode'>on</item>"
                     "<item name='mode'>on</item><item name='size'>a</item></style>"
                     "<style name='B'><item name='mode'>on</item>"
                     "<item name='com.lib:mode'>on</item><item name='size'>a|b</item></style>"
                     "</resources>"},
                    {"layout/l.xml", layout + "app:size='b' app:mode='on' lib:mode='on'/>"}},
                   "com.example", 0x7f, &errors))
      << errorsOf(errors);
  const Package& app = *table.package("com.example");
  for (const auto& [style, size] : {std::pair{"A", 1}, std::pair{"B", 3}}) {
    SCOPED_TRACE(style);
    const Style& s = app.styles[valueOf(table, std::string("com.example:style/") + style).index()];
    ASSERT_EQ(s.items.size(), 3u);
    // In the order of the <attr>s' ids, com.lib's first; aapt2 reads the two modes as 5 and 7.
    EXPECT_EQ(s.items[0].value, Value::integer(5));
    EXPECT_EQ(s.items[1].value, Value::integer(7));
    EXPECT_EQ(s.items[2].value, Value::integer(size, IntFormat::Hex));
  }

  // What aapt2 37.0.0 says, which links the XML files only once the values have linked.
  const auto messagesOf = [&](const std::vector<std::pair<std::string, std::string>>& files,
                              const std::string& name, uint8_t id) {
    errors.clear();
    EXPECT_FALSE(load(table, dir, files, name, id, &errors));
    std::vector<std::string> messages;
    for (const Diagnostic& d : errors) messages.push_back(d.message);
    return messages;
  };
  const std::string n = "expected integer but got (raw string) z";
  const std::string mode = "expected enum but got (raw string) off";
  EXPECT_EQ(messagesOf({{"values/v.xml",
                         "<resources><attr name='n' format='integer'/>"
                         "<style name='A'><item name='n'>z</item>"
                         "<item name='com.lib:mode'>off</item></style>"
                         "<style name='B'><item name='n'>z</item>"
                         "<item name='com.lib:mode'>off</item></style></resources>"}},
                       "com.other", 0x7d),
            (std::vector<std::string>{n, mode, n, mode}))
      << errorsOf(errors);
  const std::string incompatible = "'z' is incompatible with attribute n (attr) integer";
  EXPECT_EQ(
      messagesOf({{"values/v.xml", "<resources><attr name='n' format='integer'/></resources>"},
                  {"layout/l.xml", layout + "app:n='z'><b app:n='z'/></a>"}},
                 "com.another", 0x7c),
      (std::vector<std::string>{incompatible, incompatible}))
      << errorsOf(errors);
  // A value that fails to link stops link before the XML files (#165).
  EXPECT_EQ(messagesOf({{"values/v.xml",
                         "<resources><attr name='n' format='integer'/>"
                         "<style name='A'><item name='n'>z</item></style></resources>"},
                        {"layout/l.xml", layout + "app:n='z'/>"}},
                       "com.yet", 0x7b),
            (std::vector<std::string>{n}))
      << errorsOf(errors);
  // An <attr> of another app's package is named with no package too, where "not found" gives it
  // (#167).
  EXPECT_EQ(messagesOf({{"layout/l.xml", layout + "lib:mode='off'><b lib:nosuch='1'/></a>"}},
                       "com.more", 0x7a),
            (std::vector<std::string>{
                "'off' is incompatible with attribute mode (attr) enum [on=5]",
                "attribute com.lib:nosuch not found",
            }))
      << errorsOf(errors);
}

// The dump writes numbers as printf does in the C locale, whatever the host app set.
TEST(TableDump, NumbersIgnoreTheLocale) {
  TempDir dir;
  ResourceTable table;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><dimen name='d'>16.5dp</dimen>"
                     "<dimen name='f'>1.5</dimen><dimen name='z'>0</dimen>"
                     "<fraction name='p'>50%</fraction></resources>"}}));
  test::DecimalCommaLocale comma;
  HYDROID_NEED_DECIMAL_COMMA(comma);
  std::ostringstream out;
  dump(out, table);
  for (const char* number : {"16.500000dp", "() 1.5\n", "0.000000e+00", "0.500000%"}) {
    EXPECT_NE(out.str().find(number), std::string::npos) << number << " in\n" << out.str();
  }
}

TEST(TableDump, EmptyTableDumpsNothing) {
  ResourceTable table;
  std::ostringstream out;
  dump(out, table);
  EXPECT_EQ(out.str(), "");
}

}  // namespace
}  // namespace hydroid::res

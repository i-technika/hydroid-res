// ResourceTable's API, which the aapt2 golden tests see only through its dump: names and ids,
// the values and pools a load fills, references into another package, errors, and the order of
// configurations.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
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

// A package may refer to one loaded before it, by its name, as an app does to android.
TEST(ResourceTable, RefersIntoAnotherPackage) {
  TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(load(table, dir,
                   {{"values/v.xml",
                     "<resources><color name='accent'>#123</color>"
                     "<attr name='mode'><enum name='on' value='5'/></attr>"
                     "<attr name='text' format='string'/><style name='Theme'/></resources>"}},
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

// What is not well-formed XML fails with expat's message and line, as in aapt2 (#78).
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

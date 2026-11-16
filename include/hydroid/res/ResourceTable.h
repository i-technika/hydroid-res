#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/Export.h"
#include "hydroid/res/ResId.h"
#include "hydroid/res/Value.h"

// The resource table: what a res/ folder holds, as aapt2 compiles and links it (`aapt2 compile
// --dir res` then `aapt2 link`), with the same ids. A package holds types, a type entries, and an
// entry one value for each configuration it has. golden/aapt2/ holds what aapt2 makes of the
// res/ folders and values the tests load, and Aapt2TableTest checks these against it.
//
// Loading follows aapt2:
//   - Every folder of res/ is <type>[-<qualifiers>] (see parseResDir), and every file in it a
//     resource; hidden files and folders, those whose name starts with '.', and folders inside
//     those folders are skipped. In values*/, each .xml file holds resources; everywhere else
//     each file is one, named for the file up to its last '.' ("icon" of icon.png, "a.b" of
//     a.b.txt), or up to .9.png. Its path is that name and what follows the first '.' of the
//     file name: aapt2 keeps a.b.txt as res/raw/a.b.b.txt.
//   - Type ids go to the types there are, in the order of ResType from 1, styleable included,
//     although it holds nothing at runtime. Entry ids go to the entries of a type in the order of
//     their names, as bytes, from 0.
//   - <attr> and <declare-styleable> always go in the default configuration, as do the ids that
//     @+id/ makes, in values or in the attributes of an XML file, and those of the names of
//     <enum> and <flag>. Of the values with a product="", the one with none or "default" stays.
//   - A style without parent="" is the child of its name up to its last '.', which must exist.
//     Its items are read against the format of their attribute, as Android does.
//   - Every reference must name a resource of the table.
// It does not do what aapt2 link does by default beyond what the folder says, and what the golden
// files are made without: add versions of styles and XML files for the attributes they use,
// remove the values of a locale with no default, and remove values a more general configuration
// repeats (--no-auto-version, --no-version-vectors, --no-version-transitions,
// --no-resource-removal, --no-resource-deduping).
//
// Not read yet, and so an error: <public> with an id, <public-group>, <staging-public-group>,
// <overlayable>, <macro>, <add-resource>, <configVarying>, and inline XML resources
// (<aapt:attr>). The contents of XML files are not checked against the attributes they use; the
// XML trees are step 8.

namespace hydroid::res {

// The resource types, in aapt2's order, which the type ids follow.
// clang-format off
enum class ResType : uint8_t {
  Anim, Animator, Array, Attr, Bool, Color, Dimen, Drawable, Font, Fraction, Id, Integer,
  Interpolator, Layout, Menu, Mipmap, Navigation, Plurals, Raw, String, Style, Styleable,
  Transition, Xml
};
// clang-format on

HYDROID_RES_API const char* toString(ResType type);  // "anim", "string", ...
// The type of a folder or a reference ("drawable" of drawable-hdpi or @drawable/icon), or of an
// <item type="...">; case-sensitive.
HYDROID_RES_API std::optional<ResType> parseResType(std::string_view name);

// A tag in a string: <b>, or <font color="#f00">, whose name is then "font;color=#f00". It spans
// the UTF-16 units firstChar to lastChar of the text, both included. An empty tag ends one before
// it starts, as in aapt2, which makes <b></b> at the start end at 0xffffffff.
struct Span {
  std::string name;
  uint32_t firstChar = 0;
  uint32_t lastChar = 0;
};

// The value of a StyledString.
struct StyledString {
  std::string text;         // UTF-8
  std::vector<Span> spans;  // in the order their tags open
};

// What aapt2 makes of a file: it compiles XML and PNGs, and keeps the rest as they are.
enum class FileType : uint8_t {
  Other,  // raw/ files, images other than PNG, fonts
  Xml,    // any file in xml/, and .xml files elsewhere but in raw/
  Png,    // a .png or .9.png outside raw/
};

// The value of a File: where the file is in the APK, "res/drawable-hdpi-v4/icon.png".
struct FileRef {
  std::string path;
  FileType type = FileType::Other;
};

// The value of a Style. Its items are in the order of their attribute ids; aapt2 keeps an
// attribute given twice, in the order written.
struct Style {
  struct Item {
    ResId attr = 0;
    Value value;
  };
  ResId parent = 0;  // 0: none
  std::vector<Item> items;
};

// The value of an Array: <array>, <string-array> or <integer-array>.
struct Array {
  std::vector<Value> items;
};

enum class Quantity : uint8_t { Zero, One, Two, Few, Many, Other };

// The value of Plurals, by Quantity.
struct Plurals {
  std::array<std::optional<Value>, 6> items;
};

// The value of an AttrDecl: what an <attr> accepts. `formats` has the FormatBits of its
// format="" (ValueParser.h), kFormatEnum or kFormatFlags for <enum> or <flag>, and is kFormatAny
// for none.
struct AttrDecl {
  struct Symbol {
    ResId id = 0;  // the id resource of its name
    uint32_t value = 0;
    IntFormat format = IntFormat::Dec;  // how value="" was written
  };
  uint32_t formats = 0;
  int32_t min = std::numeric_limits<int32_t>::min();
  int32_t max = std::numeric_limits<int32_t>::max();
  std::vector<Symbol> symbols;  // in the order of their names
};

// The value of a StyleableDecl: the attributes of a <declare-styleable>, in the order written.
struct StyleableDecl {
  std::vector<ResId> attrs;
};

// One value of an entry, for one configuration.
struct Candidate {
  Config config;
  Value value;
};

struct Entry {
  std::string name;
  bool isPublic = false;  // <public>
  // In the order of compareConfigs, one for each configuration.
  std::vector<Candidate> candidates;
};

struct TypeTable {
  ResType type = ResType::Anim;
  uint8_t id = 0;
  std::vector<Entry> entries;  // by entry id
};

// One package of the table. A pooled Value (Value::index) is a position in the pool of its kind
// in the package that holds it.
struct Package {
  uint8_t id = 0;
  std::string name;
  std::vector<TypeTable> types;  // by type id, from 1

  std::vector<std::string> strings;  // String
  std::vector<StyledString> styledStrings;
  std::vector<FileRef> files;
  std::vector<Style> styles;
  std::vector<Array> arrays;
  std::vector<Plurals> plurals;
  std::vector<AttrDecl> attrs;  // AttrDecl
  std::vector<StyleableDecl> styleables;

  const TypeTable* type(ResType t) const {
    for (const TypeTable& table : types) {
      if (table.type == t) return &table;
    }
    return nullptr;
  }
  const Entry* entry(ResId resId) const {
    const size_t typeIndex = typeId(resId);
    const size_t entryIndex = entryId(resId);
    if (packageId(resId) != id || typeIndex == 0 || typeIndex > types.size()) return nullptr;
    const TypeTable& table = types[typeIndex - 1];
    return entryIndex < table.entries.size() ? &table.entries[entryIndex] : nullptr;
  }
};

// A problem that keeps a res/ folder from loading.
struct Diagnostic {
  std::filesystem::path file;  // empty for none
  int line = 0;                // 0 for none
  std::string message;
};

// "file:line: message", the file in UTF-8 on Windows and as its bytes elsewhere. A Windows path
// that is not Unicode text (a lone surrogate) has no UTF-8, and is written with U+FFFD for what it
// lacks rather than throwing.
HYDROID_RES_API std::ostream& operator<<(std::ostream& os, const Diagnostic& d);

class ResourceTable {
 public:
  // Loads the res/ folder at `res` as the package `name` with the id `id`, after those already
  // here, which its references may name by their package name, as aapt2 link takes them with -I.
  // On failure the table is left as it was, and the problems are added to `errors` if it is not
  // null. Pointers into the table do not survive a call that adds a package. A name in the folder
  // that is not Unicode text (on Windows) is read as aapt2 reads it: a folder of res/ so named has
  // nothing in it, and a file or folder so named in a type folder is an error, "file not found".
  HYDROID_RES_API bool addPackage(const std::filesystem::path& res, std::string_view name,
                                  uint8_t id, std::vector<Diagnostic>* errors = nullptr);

  const std::vector<Package>& packages() const { return packages_; }
  HYDROID_RES_API const Package* package(uint8_t id) const;
  HYDROID_RES_API const Package* package(std::string_view name) const;

  // The id of the resource `type`/`name` of `package` ("com.example", ResType::String,
  // "app_name"), or nullopt.
  HYDROID_RES_API std::optional<ResId> find(std::string_view package, ResType type,
                                            std::string_view name) const;
  // The id of "package:type/name", "@package:type/name" or "@type/name" in `defaultPackage`.
  HYDROID_RES_API std::optional<ResId> find(std::string_view name,
                                            std::string_view defaultPackage = {}) const;
  HYDROID_RES_API const Entry* entry(ResId id) const;
  // "com.example:string/app_name", or nullopt for an id of no resource.
  HYDROID_RES_API std::optional<std::string> nameOf(ResId id) const;

 private:
  std::vector<Package> packages_;
  std::unordered_map<std::string, ResId> byName_;  // "package:type/name"
};

// The table as `aapt2 dump resources` prints an APK, from its "Package name=" line on: what aapt2
// knows of an APK's own resources, by name, and of others only their ids, so a reference into
// another package of the table prints as its id. Styleables hold nothing at runtime, and are left
// out, as from an APK. Numbers print the same in any locale.
HYDROID_RES_API void dump(std::ostream& os, const ResourceTable& table);

// A value of `package` in an entry of `type`, as `dump` prints it after the configuration:
// "#ff6200ee", "@string/app_name", "\"Hello\"", "(style) size=2 parent=style/Base (0x7f0e0000)",
// with the lines that follow a style, array, plurals or enum or flags attribute, without their
// indent. The type matters to a string that starts with res/, which aapt2 reads back from an APK
// as a file anywhere but in a string resource.
HYDROID_RES_API std::string describe(const Package& package, ResType type, const Value& value);

}  // namespace hydroid::res

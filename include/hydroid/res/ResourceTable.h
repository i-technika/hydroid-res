#pragma once

#include <algorithm>
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
//     raw/a.b.txt), or up to .9.png. Its path is that name and what follows the first '.' of the
//     file name: aapt2 keeps raw/a.b.txt as res/raw/a.b.b.txt. Only a file kept as it is may
//     have a '.' in its name: a values file, and one compiled as XML or PNG (layout/a.b.xml,
//     drawable/a.b.png), fails the load, as aapt2 fails it. So do two files aapt2 compiles to
//     one name, as values-/a.xml and values/a.xml.
//   - Type ids go to the types there are, in the order of ResType from 1, styleable included,
//     although it holds nothing at runtime. Entry ids go to the entries of a type in the order of
//     their names, as bytes, from 0. An id a <public id=""> gives is kept, and left out of those
//     the others take, which are then the lowest left. It must be of the package, of the one type
//     id of its type, and of no other entry. A file can give an entry one id, and files that make
//     it public the same one, but once a file has made it public without one, a later file's id
//     is left out, as aapt2 link merges them. The last entry id, 0xffff, is one as any other: a
//     <public id=""> can give it, and the 65536th entry of a type takes it. aapt2 gives it too,
//     then stops on a check of its own as it writes the binary table, with no error (#190): it
//     builds a folder that has one only with --proto-format, and with the same ids.
//   - <attr> and <declare-styleable> always go in the default configuration, as do the ids that
//     @+id/ makes, in values or in the attributes of an XML file, and those of the names of
//     <enum> and <flag>. The id a <public type="id"> defines goes in the configuration of its
//     folder. Of the values with a product="", the one with none or "default" stays. An <attr>
//     of product="default" stays, and a reference finds it, but a style item or an attribute of
//     an XML file that names it is told it is not an attribute, as aapt2 says.
//   - A style without parent="" is the child of its name up to its last '.', which must exist.
//     Its items are read against the format of their attribute, as Android does.
//   - Every reference must name a resource of the table. One of another package must be <public>
//     there, but for a private reference: @*, * before a style parent or an attribute's name, or
//     a prefix or attribute of an http://schemas.android.com/apk/prv/res/ namespace.
//   - The attributes of XML files are linked as aapt2 links them, but those of tools:, which it
//     leaves alone: one of a package's namespace (android:text, or app:x of res-auto) must name
//     an <attr> of the table, and its value be one that <attr> takes; any reference in one must
//     name a resource of the table. What one names in android is left alone while no package
//     android is loaded, as hydroid cannot read android.jar yet.
// It does not do what aapt2 link does by default beyond what the folder says, and what the golden
// files are made without: add versions of styles and XML files for the attributes they use,
// remove the values of a locale with no default, and remove values a more general configuration
// repeats (--no-auto-version, --no-version-vectors, --no-version-transitions,
// --no-resource-removal, --no-resource-deduping).
//
// A locale with a variant is a configuration of its own, as aapt2 keeps it. The Android Gradle
// plugin (9.5.1) compiles values and most files with a compiler of its own, which writes the
// variant of a locale with no script as empty: an app it builds has values-b+de+DE+1901/ as
// values-de/, and raw-b+de+DE+1901/ as raw-de/, and it fails on values-b+de+1901/ (#198). hydroid
// does not do that, and so can pick another value from such a folder than that app does.
//
// Not read yet, and so an error: <public-group>, <staging-public-group>, <overlayable>, <macro>,
// <add-resource>, <configVarying>, and inline XML resources (<aapt:attr>). The table keeps an XML
// file as a file, and nothing of its contents; the XML trees are step 8.

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
  uint16_t id = 0;  // its entry id
  std::string name;
  bool isPublic = false;  // <public>
  // In the order of compareConfigs, one for each configuration.
  std::vector<Candidate> candidates;
};

struct TypeTable {
  ResType type = ResType::Anim;
  uint8_t id = 0;
  // In the order of their ids, which leave out those no entry has: those before an id a <public>
  // gives, or between two.
  std::vector<Entry> entries;
};

// One package of the table. A pooled Value (Value::index) is a position in the pool of its kind
// in the package that holds it.
struct Package {
  uint8_t id = 0;
  std::string name;
  std::vector<TypeTable> types;  // in the order of their ids, as the entries of one

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
  // The type of the type id `typeId`, or nullptr.
  const TypeTable* typeWithId(uint8_t typeId) const {
    for (const TypeTable& table : types) {
      if (table.id == typeId) return &table;
    }
    return nullptr;
  }
  const Entry* entry(ResId resId) const {
    const TypeTable* table = packageId(resId) == id ? typeWithId(typeId(resId)) : nullptr;
    if (!table) return nullptr;
    const auto it = std::lower_bound(table->entries.begin(), table->entries.end(), entryId(resId),
                                     [](const Entry& e, uint16_t entry) { return e.id < entry; });
    return it != table->entries.end() && it->id == entryId(resId) ? &*it : nullptr;
  }
};

// A problem that keeps a res/ folder from loading. There are no warnings: where aapt2 only warns
// and goes on, as of an id @+android:id/ makes in an XML file or a <public> in a folder with
// qualifiers, hydroid goes on too and says nothing (#130), where it reads what is warned of at
// all: what it does not read yet, as <public-group> or <overlayable>, is an error in any folder.
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
  // here, which its references may name by their package name, as aapt2 link takes them with -I:
  // what such a package makes <public>, and anything else only by a private reference.
  // On failure the table is left as it was, and the problems are added to `errors` if it is not
  // null. Pointers into the table do not survive a call that adds a package. A name in the folder
  // that is not Unicode text (on Windows) is read as aapt2 reads it: a folder of res/ so named has
  // nothing in it, and a file or folder so named in a type folder is an error, "file not found".
  // Nothing about the folder throws: a folder that cannot be opened or read, a file that cannot be
  // read, is an error in `errors` too, where aapt2 on Windows leaves a folder it cannot open out.
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

  // A number that changes whenever what the table holds may: when it adds a package, and when it
  // is assigned or moved from. It never comes back to a value it had in this table, so what is
  // kept of the table can be kept by it, as Resources keeps what it picked.
  uint64_t revision() const { return revision_.value; }

 private:
  // Holds the revision through the table's implicit copies and moves: a table copied or moved
  // into takes the revision of the one it is made from, and one assigned to, or moved from, goes
  // to its next.
  struct Revision {
    uint64_t value = 0;

    Revision() = default;
    Revision(const Revision&) = default;
    Revision(Revision&& other) noexcept : value(other.value) { other.value++; }
    Revision& operator=(const Revision&) noexcept {
      value++;
      return *this;
    }
    Revision& operator=(Revision&& other) noexcept {
      value++;
      if (&other != this) other.value++;
      return *this;
    }
  };

  // First, so that the implicit assignments change it before what it holds: one that throws
  // part of the way has changed it all the same.
  Revision revision_;
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

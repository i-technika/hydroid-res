// What the scanner and the values parser hand the table builder: resources as aapt2 compile leaves
// them, which name the resources they refer to where the table will have ids.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Value.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res::detail {

// Where something was read, for errors.
struct Source {
  std::filesystem::path file;
  int line = 0;
};

struct ParsedFolder;

// Collects the problems of a load.
class Diagnostics {
 public:
  // Where an error of reading a res/ folder falls among what was read of it: after so many of its
  // resources and symbols.
  struct Position {
    size_t resources = 0;
    size_t symbols = 0;
  };

  void error(const Source& where, std::string message);
  bool empty() const { return errors_.empty(); }
  const std::vector<Diagnostic>& errors() const { return errors_; }
  // The Position of each error in the folder being read when it was given, or {} with none.
  const std::vector<Position>& positions() const { return positions_; }
  // Gives each error from now on the Position of what `folder` holds then, or none with nullptr,
  // for compile to give the errors of reading among those of each file's table (#156).
  void track(const ParsedFolder* folder) { folder_ = folder; }

 private:
  std::vector<Diagnostic> errors_;
  std::vector<Position> positions_;
  const ParsedFolder* folder_ = nullptr;
};

// A resource by name, @[package:]type/entry. An empty package is the one being loaded.
struct ResName {
  ResName() = default;
  ResName(std::string inPackage, ResType inType, std::string inEntry,
          std::string_view inOtherType = {})
      : package(std::move(inPackage)),
        type(inType),
        entry(std::move(inEntry)),
        otherType(inOtherType) {}

  std::string package;
  ResType type = ResType::Anim;
  std::string entry;
  // In a reference, a type aapt2 reads that no ResType stands for, as configVarying, in place of
  // `type`: no package hydroid loads has a resource of it, so that the reference is not found
  // (#142). android.jar has ^attr-private, which hydroid cannot read yet (#98).
  std::string_view otherType;
  // In a reference, aapt2's private_reference: @* or * before the name, or a package of an
  // http://schemas.android.com/apk/prv/res/ namespace. Such a reference may name a resource
  // another package does not make <public>, which any other fails to (#141).
  bool privateReference = false;

  std::string typeName() const {
    return otherType.empty() ? toString(type) : std::string(otherType);
  }
};

// A value that fits one Res_value, before linking.
struct ParsedItem {
  enum class Kind : uint8_t {
    Literal,       // `literal`: @null, @empty, a color, bool, integer, float, dimension, fraction
    Reference,     // @`ref`
    AttrRef,       // ?`ref`
    String,        // `text`
    StyledString,  // `styled`
    Raw,           // `text`, a style item to read against the format of its attribute
    Id,            // the value of an id resource
  };
  Kind kind = Kind::Literal;
  Value literal;
  ResName ref;
  std::string text;
  StyledString styled;
  Source source;
};

struct ParsedStyle {
  struct Item {
    ResName attr;
    ParsedItem value;
  };
  std::optional<ResName> parent;
  std::vector<Item> items;
};

struct ParsedArray {
  std::vector<ParsedItem> items;
};

struct ParsedPlurals {
  std::array<std::optional<ParsedItem>, 6> items;
};

struct ParsedAttr {
  struct Symbol {
    std::string name;  // of an id resource
    uint32_t value = 0;
    IntFormat format = IntFormat::Dec;
    Source source;
  };
  uint32_t formats = kFormatAny;
  // An <attr> of a <declare-styleable>, which yields to another of its file, as aapt2 compile
  // has it, but not to one of another file, which aapt2 link merges (#81).
  bool weak = false;
  int32_t min = std::numeric_limits<int32_t>::min();
  int32_t max = std::numeric_limits<int32_t>::max();
  std::vector<Symbol> symbols;  // in the order of their names
};

struct ParsedStyleable {
  std::vector<ResName> attrs;
};

using ParsedValue = std::variant<ParsedItem, ParsedStyle, ParsedArray, ParsedPlurals, ParsedAttr,
                                 ParsedStyleable, FileRef>;

struct ParsedResource {
  ResType type = ResType::Anim;
  std::string name;
  Config config;
  std::string product;  // product="", which only the table builder looks at
  // The package written before the name, resolved, of an <attr> a <declare-styleable> defines:
  // "" or the package being loaded defines it there, and another package's is left out. And the
  // package of an id @+pkg:id/ makes, as written: one a values file makes is left out in another
  // package as well (#131), and one an XML file makes fails there, but in android (#83).
  std::string package;
  bool inXmlFile = false;  // an id an XML file makes, for the check of its package
  // A resource of a values file, whose name the table builder checks as AddResourcesToTable does,
  // when it adds it: the scanner checks those of the ids it makes, with words of its own.
  bool checkName = false;
  // How many of the resources that follow are its own: aapt2's child_resources, each followed by
  // its own in turn, which go in only if it does (#159).
  size_t children = 0;
  ParsedValue value;
  Source source;
};

// <public type="" name=""/>, or <java-symbol> or <symbol>, which make it private: each of a
// resource that must have a value.
struct SymbolDecl {
  ResType type = ResType::Anim;
  std::string name;
  bool isPublic = false;
  std::optional<ResId> id;  // <public id="">
  Source source;
  // How many resources of the folder were read before it, which places it among them, as aapt2
  // compile adds them to the table of a file in the order written (#154).
  size_t position = 0;
};

// An attribute of an XML file as aapt2's XmlReferenceLinker links it (#132): the <attr> its
// namespace names, and its value, read against that <attr>, or as anything without one. The XML
// file itself is not compiled yet (#100), so the table keeps nothing of it: a link that fails
// only fails the load, as it fails aapt2 link.
struct ParsedXmlAttribute {
  // pkg:attr/name of an attribute in a namespace of a package (namespacePackage), the package ""
  // for http://schemas.android.com/apk/res-auto.
  std::optional<ResName> attr;
  // A Literal for @null and @empty, a Reference or AttrRef with its package resolved as an XML
  // namespace prefix, or the text of anything else as written, Raw.
  ParsedItem value;
};

// What a res/ folder holds, in the order read: the resources of a file together, which the table
// builder merges as aapt2 compile does the file before link merges the files (#88).
struct ParsedFolder {
  std::vector<ParsedResource> resources;
  std::vector<SymbolDecl> symbols;
  std::vector<ParsedXmlAttribute> xmlAttributes;
  // An error of each file that compiles to the .flat name of a file before it, as values/a.xml to
  // that of values-/a.xml (#122). aapt2 compiles both, and link, which only runs if compile gave
  // no error, fails on the archive that holds the name twice before anything else (#158).
  std::vector<Diagnostic> sameFlatName;
};

inline void Diagnostics::error(const Source& where, std::string message) {
  errors_.push_back({where.file, where.line, std::move(message)});
  positions_.push_back(folder_ ? Position{folder_->resources.size(), folder_->symbols.size()}
                               : Position{});
}

// aapt2's ResourceUtils::IsValidResourceEntryName for ASCII: letters, digits, '_', '.' and '-',
// not starting with a digit. aapt2 takes the other characters that the host's iswalnum calls
// letters or digits ("é", "가", but not "€"), which this takes them all for.
bool isValidEntryName(std::string_view name);

// The id resource that @+id/`name`, in a values file or an attribute of an XML file, or an <enum>
// or <flag> makes, or a <public type="id"> defines: weak, its value only standing for its ResId.
// It is in `config`: the default configuration but for that of a <public>, which is in that of
// its folder, as any value there is (#82), and has the `product` of that <public>, which
// ProductFilter checks as any value's (#136). The one place that says what such a resource is,
// for the values parser and the scanner (#90).
ParsedResource makeIdResource(std::string name, const Source& source, const Config& config = {},
                              std::string product = {});

// Attribute::MaskString: "reference|color", "any" for kFormatAny.
std::string formatsToString(uint32_t formats);

}  // namespace hydroid::res::detail

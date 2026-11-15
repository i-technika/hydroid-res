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

// Collects the problems of a load.
class Diagnostics {
 public:
  void error(const Source& where, std::string message) {
    errors_.push_back({where.file, where.line, std::move(message)});
  }
  bool empty() const { return errors_.empty(); }
  std::vector<Diagnostic>& errors() { return errors_; }

 private:
  std::vector<Diagnostic> errors_;
};

// A resource by name, @[package:]type/entry. An empty package is the one being loaded.
struct ResName {
  std::string package;
  ResType type = ResType::Anim;
  std::string entry;
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
  // An <attr> of a <declare-styleable>, which yields to one given anywhere else.
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
  ParsedValue value;
  Source source;
};

// <public type="" name=""/>, or <java-symbol> or <symbol>, which make it private: each of a
// resource that must have a value.
struct SymbolDecl {
  ResType type = ResType::Anim;
  std::string name;
  bool isPublic = false;
  Source source;
};

// What a res/ folder holds, in the order read.
struct ParsedFolder {
  std::vector<ParsedResource> resources;
  std::vector<SymbolDecl> symbols;
};

// aapt2's ResourceUtils::IsValidResourceEntryName for ASCII: letters, digits, '_', '.' and '-',
// not starting with a digit. aapt2 takes the other characters that the host's iswalnum calls
// letters or digits ("é", "가", but not "€"), which this takes them all for.
bool isValidEntryName(std::string_view name);

// Attribute::MaskString: "reference|color", "any" for kFormatAny.
std::string formatsToString(uint32_t formats);

}  // namespace hydroid::res::detail

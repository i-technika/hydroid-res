#include "core/TableBuilder.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "core/ValuesParser.h"
#include "core/Whitespace.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res::detail {
namespace {

using Key = std::pair<ResType, std::string>;  // ordered as aapt2 gives ids: type, then name

struct PendingValue {
  Config config;
  std::string product;
  ParsedValue value;
  Source source;
};

struct PendingEntry {
  std::vector<PendingValue> values;
  bool isPublic = false;
};

std::string nameOf(ResType type, const std::string& entry) {
  return std::string(toString(type)) + "/" + entry;
}

// Where two values of one configuration and product meet: in one file, which aapt2 compile turns
// into a table of its own, or in two, whose tables aapt2 link merges.
enum class Stage : uint8_t { Compile, Link };

// Ids and, in the file that defines them, the <attr>s of <declare-styleable>s, which yield to any
// other value. aapt2 link sees no <attr> as weak (#81): of two that agree, it keeps the first.
bool isWeak(const ParsedValue& v, Stage stage) {
  if (const auto* item = std::get_if<ParsedItem>(&v)) return item->kind == ParsedItem::Kind::Id;
  if (const auto* attr = std::get_if<ParsedAttr>(&v)) return attr->weak && stage == Stage::Compile;
  return false;
}

// Attribute::IsCompatibleWith: two plain attributes of the same formats, but for reference.
bool compatible(const ParsedAttr& a, const ParsedAttr& b) {
  if ((a.formats & ~kFormatAny) != 0 || (b.formats & ~kFormatAny) != 0) return false;
  return (a.formats | kFormatReference) == (b.formats | kFormatReference);
}

enum class Collision : uint8_t { KeepOriginal, TakeNew, Conflict };

// ResourceTable::ResolveValueCollision, for two values of one configuration and product.
Collision resolveCollision(const ParsedValue& existing, const ParsedValue& incoming, Stage stage) {
  const auto* existingAttr = std::get_if<ParsedAttr>(&existing);
  const auto* incomingAttr = std::get_if<ParsedAttr>(&incoming);
  const bool existingWeak = isWeak(existing, stage);
  const bool incomingWeak = isWeak(incoming, stage);
  if (!incomingAttr) {
    if (incomingWeak) return Collision::KeepOriginal;
    return existingWeak ? Collision::TakeNew : Collision::Conflict;
  }
  if (!existingAttr) return existingWeak ? Collision::TakeNew : Collision::Conflict;
  if (compatible(*existingAttr, *incomingAttr)) {
    return existingWeak ? Collision::TakeNew : Collision::KeepOriginal;
  }
  if (existingWeak && existingAttr->formats == kFormatAny) return Collision::TakeNew;
  if (incomingWeak && incomingAttr->formats == kFormatAny) return Collision::KeepOriginal;
  return Collision::Conflict;
}

// Puts `incoming` among the `values` of one entry, as `stage` resolves its collision with the value
// of its configuration and product. False on a conflict, `incoming` left as it was.
bool merge(std::vector<PendingValue>& values, PendingValue& incoming, Stage stage) {
  const auto same = std::find_if(values.begin(), values.end(), [&](const PendingValue& v) {
    return v.config == incoming.config && v.product == incoming.product;
  });
  if (same == values.end()) {
    values.push_back(std::move(incoming));
    return true;
  }
  switch (resolveCollision(same->value, incoming.value, stage)) {
    case Collision::KeepOriginal: return true;
    case Collision::TakeNew: *same = std::move(incoming); return true;
    case Collision::Conflict: return false;
  }
  return false;
}

// The kinds of value a Value is, as attribute formats (AndroidTypeToAttributeTypeMask of the
// Res_value it flattens to).
uint32_t formatsOfValue(const Value& v) {
  switch (v.kind()) {
    case ValueKind::Null:
    case ValueKind::Empty:
    case ValueKind::Reference:
    case ValueKind::AttrRef: return kFormatReference;
    case ValueKind::String:
    case ValueKind::StyledString:
    case ValueKind::File: return kFormatString;
    case ValueKind::Id:
    case ValueKind::Bool: return kFormatBoolean;
    case ValueKind::Color: return kFormatColor;
    case ValueKind::Dimension: return kFormatDimension;
    case ValueKind::Fraction: return kFormatFraction;
    case ValueKind::Float: return kFormatFloat;
    case ValueKind::Int: return kFormatInteger | kFormatEnum | kFormatFlags;
    case ValueKind::Style:
    case ValueKind::Array:
    case ValueKind::Plurals:
    case ValueKind::AttrDecl:
    case ValueKind::StyleableDecl: return 0;
  }
  return 0;
}

// What a style item is read against: an <attr> of this package or another.
struct AttrInfo {
  struct Symbol {
    std::string name;
    uint32_t value;
    IntFormat format;
  };
  uint32_t formats = kFormatAny;
  int32_t min = 0;
  int32_t max = 0;
  std::vector<Symbol> symbols;
};

class Linker {
 public:
  Linker(Package& package, const std::map<Key, std::vector<PendingValue>>& entries,
         const std::map<Key, ResId>& ids, const ResourceTable& deps, Diagnostics& diag)
      : package_(package), entries_(entries), ids_(ids), deps_(deps), diag_(diag) {}

  std::optional<Value> link(const ParsedValue& value, const Source& source) {
    struct Visitor {
      Linker& l;
      const Source& source;
      std::optional<Value> operator()(const ParsedItem& v) { return l.linkItem(v); }
      std::optional<Value> operator()(const ParsedStyle& v) { return l.linkStyle(v, source); }
      std::optional<Value> operator()(const ParsedArray& v) { return l.linkArray(v); }
      std::optional<Value> operator()(const ParsedPlurals& v) { return l.linkPlurals(v); }
      std::optional<Value> operator()(const ParsedAttr& v) { return l.linkAttr(v, source); }
      std::optional<Value> operator()(const ParsedStyleable& v) {
        return l.linkStyleable(v, source);
      }
      std::optional<Value> operator()(const FileRef& v) {
        l.package_.files.push_back(v);
        return Value::file(static_cast<uint32_t>(l.package_.files.size() - 1));
      }
    };
    return std::visit(Visitor{*this, source}, value);
  }

  // XmlReferenceLinker for one attribute of an XML file: the <attr> its namespace names must be
  // found, a reference in its value must resolve, and any other value must be one the <attr>
  // takes. An attribute of no package's namespace takes anything but an enum or flags name, and
  // so takes any text, as a string. Unlike a style item's, the value is not checked against the
  // <attr>'s min and max, nor its type against a reference's. Problems go to the diagnostics.
  void linkXmlAttribute(const ParsedXmlAttribute& attribute) {
    const ParsedItem& value = attribute.value;
    std::optional<AttrInfo> info;
    std::string attrName;
    if (attribute.attr && linksInto(attribute.attr->package)) {
      const ResName& attr = *attribute.attr;
      attrName = (own(attr.package) ? "" : attr.package + ":") + attr.entry;
      const std::optional<ResId> id = resolve(attr, value.source, "attribute");
      if (!id) return;
      info = attrInfo(attr, *id);
      if (!info) {
        diag_.error(value.source, "attribute " + attrName + " is not an <attr>");
        return;
      }
    }
    switch (value.kind) {
      case ParsedItem::Kind::Reference:
      case ParsedItem::Kind::AttrRef:
        if (linksInto(value.ref.package)) resolve(value.ref, value.source);
        return;
      case ParsedItem::Kind::Raw:
        if (info && !(info->formats & kFormatString) && !parseForAttr(value.text, *info)) {
          diag_.error(value.source, "'" + value.text + "' is incompatible with attribute " +
                                        attrName + " " + formatsToString(info->formats));
        }
        return;
      case ParsedItem::Kind::Literal:
      case ParsedItem::Kind::String:
      case ParsedItem::Kind::StyledString:
      case ParsedItem::Kind::Id: return;
    }
  }

 private:
  bool own(const std::string& package) const { return package.empty() || package == package_.name; }

  // Whether what an XML file names in `package` is linked. aapt2 links every app against
  // android.jar, which hydroid cannot read yet: what an XML file names in android, as the
  // attribute android:text, is left alone while no package android is loaded. A reference of a
  // values file is not, as it becomes a value of the table, which needs its id.
  bool linksInto(const std::string& package) const {
    return own(package) || package != "android" || deps_.package(package);
  }

  std::optional<ResId> resolve(const ResName& name, const Source& source,
                               const char* what = "resource") {
    const std::string written =
        (own(name.package) ? "" : name.package + ":") + name.typeName() + "/" + name.entry;
    // aapt2 looks for a macro to put its contents in place of the reference, in any package, and
    // says so when there is none.
    if (name.otherType == "macro") {
      diag_.error(source, "failed to find definition for " + written);
      return std::nullopt;
    }
    // No package hydroid loads has a resource of a type no ResType stands for (#142), so that a
    // reference of one is not looked for. android.jar has ^attr-private, which aapt2 finds: the
    // table must keep that type before a reference finds it there (#98).
    const bool kept = name.otherType.empty();
    if (own(name.package)) {
      const auto it = ids_.find({name.type, name.entry});
      if (kept && it != ids_.end()) return it->second;
    } else if (!deps_.package(name.package)) {
      diag_.error(source, std::string(what) + " " + written + " not found: no package '" +
                              name.package + "' is loaded");
      return std::nullopt;
    } else if (kept) {
      if (std::optional<ResId> id = deps_.find(name.package, name.type, name.entry)) {
        // ReferenceLinker::IsSymbolVisible: what another package does not make <public> only a
        // private reference may name (#141).
        if (name.privateReference || deps_.entry(*id)->isPublic) return id;
        diag_.error(source, std::string(what) + " " + written + " is private");
        return std::nullopt;
      }
    }
    diag_.error(source, std::string(what) + " " + written + " not found");
    return std::nullopt;
  }

  uint32_t pooled(const std::string& s) {
    const auto [it, added] =
        stringIndex_.emplace(s, static_cast<uint32_t>(package_.strings.size()));
    if (added) package_.strings.push_back(s);
    return it->second;
  }

  std::optional<Value> linkItem(const ParsedItem& item) {
    switch (item.kind) {
      case ParsedItem::Kind::Literal: return item.literal;
      case ParsedItem::Kind::Reference:
      case ParsedItem::Kind::AttrRef: {
        const std::optional<ResId> id = resolve(item.ref, item.source);
        if (!id) return std::nullopt;
        return item.kind == ParsedItem::Kind::Reference ? Value::reference(*id)
                                                        : Value::attrRef(*id);
      }
      case ParsedItem::Kind::String: return Value::string(pooled(item.text));
      case ParsedItem::Kind::StyledString:
        package_.styledStrings.push_back(item.styled);
        return Value::styledString(static_cast<uint32_t>(package_.styledStrings.size() - 1));
      case ParsedItem::Kind::Id: return Value::id();
      case ParsedItem::Kind::Raw: break;
    }
    diag_.error(item.source, "text to read against an attribute, outside a style");
    return std::nullopt;
  }

  // The <attr> a style item names, or nullopt if `id` is not one.
  std::optional<AttrInfo> attrInfo(const ResName& name, ResId id) {
    AttrInfo info;
    if (own(name.package)) {
      for (const PendingValue& v : entries_.at({ResType::Attr, name.entry})) {
        const auto* attr = std::get_if<ParsedAttr>(&v.value);
        if (!attr) continue;
        info.formats = attr->formats;
        info.min = attr->min;
        info.max = attr->max;
        for (const ParsedAttr::Symbol& s : attr->symbols) {
          info.symbols.push_back({s.name, s.value, s.format});
        }
        return info;
      }
      return std::nullopt;
    }
    const Entry* entry = deps_.entry(id);
    const Package* package = deps_.package(packageId(id));
    for (const Candidate& c : entry->candidates) {
      if (c.value.kind() != ValueKind::AttrDecl) continue;
      const AttrDecl& attr = package->attrs[c.value.index()];
      info.formats = attr.formats;
      info.min = attr.min;
      info.max = attr.max;
      for (const AttrDecl::Symbol& s : attr.symbols) {
        info.symbols.push_back({deps_.entry(s.id)->name, s.value, s.format});
      }
      return info;
    }
    return std::nullopt;
  }

  // TryParseItemForAttribute but for references: `text` as the literal, enum or flags `attr`
  // takes, each read with whitespace around.
  static std::optional<Value> parseForAttr(std::string_view text, const AttrInfo& attr) {
    if (std::optional<Value> v = parseLiteral(text, attr.formats)) return v;
    const std::string_view t = trim(text);
    if (attr.formats & kFormatEnum) {
      for (const AttrInfo::Symbol& s : attr.symbols) {
        if (s.name == t) return Value::integer(static_cast<int32_t>(s.value), s.format);
      }
    }
    if (attr.formats & kFormatFlags) {
      if (std::optional<uint32_t> flags = flagsOf(t, attr)) {
        return Value::integer(static_cast<int32_t>(*flags), IntFormat::Hex);
      }
    }
    return std::nullopt;
  }

  // ReferenceLinker's ParseValueWithAttribute: a style item's text as the literal, reference,
  // enum or flags its attribute takes, else a string if it takes one.
  std::optional<Value> coerce(const ParsedItem& raw, const AttrInfo& attr) {
    const std::string& text = raw.text;
    // Compile has read @null, @empty and every reference but @+id/ of a name no id can have (#91).
    // aapt2 reads that one here, after it has resolved references, so it stays unresolved and
    // flattens to 0: @null.
    if (parseReference(text)) return Value::null();
    if (std::optional<Value> v = parseForAttr(text, attr)) return v;
    if (attr.formats & kFormatString) {
      // aapt2 keeps the text as written when it is not a valid string, an apostrophe and all.
      std::string why;
      const std::optional<std::string> s = buildString(text, &why);
      return Value::string(pooled(s ? *s : text));
    }
    diag_.error(raw.source,
                "expected " + formatsToString(attr.formats) + " but got raw string " + text);
    return std::nullopt;
  }

  // TryParseFlagSymbol: names of flags joined by '|', each with whitespace around; "" is none.
  static std::optional<uint32_t> flagsOf(std::string_view text, const AttrInfo& attr) {
    uint32_t flags = 0;
    if (text.empty()) return flags;
    for (;;) {
      const size_t bar = text.find('|');
      const std::string_view part = trim(text.substr(0, bar));
      const auto symbol = std::find_if(attr.symbols.begin(), attr.symbols.end(),
                                       [&](const AttrInfo::Symbol& s) { return s.name == part; });
      if (symbol == attr.symbols.end()) return std::nullopt;
      flags |= symbol->value;
      if (bar == std::string_view::npos) return flags;
      text.remove_prefix(bar + 1);
    }
  }

  // Attribute::Matches.
  static bool matches(const AttrInfo& attr, const Value& v, std::string* why) {
    if ((formatsOfValue(v) & (attr.formats | kFormatReference)) == 0) {
      *why = "expected " + formatsToString(attr.formats);
      return false;
    }
    if (v.kind() != ValueKind::Int) return true;
    const auto data = static_cast<uint32_t>(v.intValue());
    if (attr.formats & kFormatEnum) {
      for (const AttrInfo::Symbol& s : attr.symbols) {
        if (s.value == data) return true;
      }
      if (!(attr.formats & kFormatInteger)) {
        *why = "not a valid enum";
        return false;
      }
    }
    if (attr.formats & kFormatFlags) {
      uint32_t mask = 0;
      for (const AttrInfo::Symbol& s : attr.symbols) mask |= s.value;
      if ((mask & data) == data) return true;
      if (!(attr.formats & kFormatInteger)) {
        *why = "not a valid flag";
        return false;
      }
    }
    if (attr.formats & kFormatInteger) {
      if (v.intValue() < attr.min) {
        *why = "less than the minimum " + std::to_string(attr.min);
        return false;
      }
      if (v.intValue() > attr.max) {
        *why = "greater than the maximum " + std::to_string(attr.max);
        return false;
      }
    }
    return true;
  }

  std::optional<Value> linkStyle(const ParsedStyle& parsed, const Source& source) {
    Style out;
    bool ok = true;
    if (parsed.parent) {
      if (std::optional<ResId> parent = resolve(*parsed.parent, source)) {
        out.parent = *parent;
      } else {
        ok = false;
      }
    }
    for (const ParsedStyle::Item& item : parsed.items) {
      const std::optional<ResId> attrId = resolve(item.attr, item.value.source, "style attribute");
      if (!attrId) {
        ok = false;
        continue;
      }
      const std::optional<AttrInfo> info = attrInfo(item.attr, *attrId);
      if (!info) {
        diag_.error(item.value.source, "style attribute " + nameOf(ResType::Attr, item.attr.entry) +
                                           " is not an <attr>");
        ok = false;
        continue;
      }
      std::optional<Value> value = item.value.kind == ParsedItem::Kind::Raw
                                       ? coerce(item.value, *info)
                                       : linkItem(item.value);
      if (!value) {
        ok = false;
        continue;
      }
      std::string why;
      if (!matches(*info, *value, &why)) {
        diag_.error(item.value.source,
                    "style attribute " + nameOf(ResType::Attr, item.attr.entry) + ": " + why);
        ok = false;
        continue;
      }
      out.items.push_back({*attrId, *value});
    }
    if (!ok) return std::nullopt;
    std::stable_sort(out.items.begin(), out.items.end(),
                     [](const Style::Item& a, const Style::Item& b) { return a.attr < b.attr; });
    package_.styles.push_back(std::move(out));
    return Value::style(static_cast<uint32_t>(package_.styles.size() - 1));
  }

  std::optional<Value> linkArray(const ParsedArray& parsed) {
    Array out;
    bool ok = true;
    for (const ParsedItem& i : parsed.items) {
      std::optional<Value> v = linkItem(i);
      ok &= v.has_value();
      if (v) out.items.push_back(*v);
    }
    if (!ok) return std::nullopt;
    package_.arrays.push_back(std::move(out));
    return Value::array(static_cast<uint32_t>(package_.arrays.size() - 1));
  }

  std::optional<Value> linkPlurals(const ParsedPlurals& parsed) {
    Plurals out;
    bool ok = true;
    for (size_t i = 0; i < parsed.items.size(); i++) {
      if (!parsed.items[i]) continue;
      out.items[i] = linkItem(*parsed.items[i]);
      ok &= out.items[i].has_value();
    }
    if (!ok) return std::nullopt;
    package_.plurals.push_back(std::move(out));
    return Value::plurals(static_cast<uint32_t>(package_.plurals.size() - 1));
  }

  std::optional<Value> linkAttr(const ParsedAttr& parsed, const Source& source) {
    AttrDecl out;
    out.formats = parsed.formats;
    out.min = parsed.min;
    out.max = parsed.max;
    for (const ParsedAttr::Symbol& s : parsed.symbols) {
      const std::optional<ResId> id = resolve({"", ResType::Id, s.name}, source);
      if (!id) return std::nullopt;
      out.symbols.push_back({*id, s.value, s.format});
    }
    package_.attrs.push_back(std::move(out));
    return Value::attrDecl(static_cast<uint32_t>(package_.attrs.size() - 1));
  }

  std::optional<Value> linkStyleable(const ParsedStyleable& parsed, const Source& source) {
    StyleableDecl out;
    bool ok = true;
    for (const ResName& name : parsed.attrs) {
      const std::optional<ResId> id = resolve(name, source);
      ok &= id.has_value();
      if (id) out.attrs.push_back(*id);
    }
    if (!ok) return std::nullopt;
    package_.styleables.push_back(std::move(out));
    return Value::styleableDecl(static_cast<uint32_t>(package_.styleables.size() - 1));
  }

  Package& package_;
  const std::map<Key, std::vector<PendingValue>>& entries_;
  const std::map<Key, ResId>& ids_;
  const ResourceTable& deps_;
  Diagnostics& diag_;
  std::unordered_map<std::string, uint32_t> stringIndex_;
};

}  // namespace

std::optional<Package> buildPackage(ParsedFolder folder, const std::string& name, uint8_t id,
                                    const ResourceTable& deps, Diagnostics& diag) {
  // Every value of every entry, one for each configuration and product.
  std::map<Key, PendingEntry> entries;
  // The ids XML files make in other packages, by package and name, each where first made.
  std::map<std::pair<std::string, std::string>, Source> externalIds;
  // aapt2 compiles each file into a table of its own, where a weak value yields to another of the
  // file, then link merges the tables in the order of the files, where an <attr> is not weak
  // (#88). The resources of a file are together, in the order the files are read (#80).
  std::vector<ParsedResource>& resources = folder.resources;
  for (size_t begin = 0, end = 0; begin < resources.size(); begin = end) {
    const std::filesystem::path& file = resources[begin].source.file;
    for (end = begin + 1; end < resources.size() && resources[end].source.file == file;) end++;

    // The file's table: of each package as written ("" before any other), type and name, one value
    // for each configuration and product.
    std::map<std::pair<std::string, Key>, std::vector<PendingValue>> compiled;
    for (size_t i = begin; i < end; i++) {
      ParsedResource& r = resources[i];
      // An id an XML file makes in another package fails link, but in android, which it only
      // warns of (#83).
      if (r.inXmlFile && !r.package.empty() && r.package != name && r.package != "android") {
        externalIds.emplace(std::pair(r.package, r.name), r.source);
      }
      PendingValue value{r.config, r.product, std::move(r.value), r.source};
      if (!merge(compiled[{r.package, {r.type, r.name}}], value, Stage::Compile)) {
        diag.error(r.source, "duplicate value for resource '" +
                                 (r.package.empty() ? "" : r.package + ":") +
                                 nameOf(r.type, r.name) + "' with config '" + toString(r.config) +
                                 "'");
      }
    }
    for (auto& [qualified, values] : compiled) {
      const auto& [package, key] = qualified;
      // aapt2 link merges only what is defined in the package being linked, written or not, after
      // compile has resolved what the file defines in each (#109): an <attr> a
      // <declare-styleable> defines in another package is left out, and the styleable then finds
      // that package's attr, or fails to. So is an id @+pkg:id/ makes, and a reference to it
      // then fails (#131).
      if (!package.empty() && package != name) continue;
      for (PendingValue& value : values) {
        if (!merge(entries[key].values, value, Stage::Link)) {
          diag.error(value.source, "resource '" + nameOf(key.first, key.second) +
                                       "' has a conflicting value for configuration (" +
                                       toString(value.config) + ")");
        }
      }
    }
  }
  // One error for each such id, after merging and in the order of package and name, as aapt2's
  // VerifyNoExternalPackages gives them from the merged table.
  for (const auto& [external, source] : externalIds) {
    const auto& [package, entry] = external;
    diag.error(source, "defined resource '" + package + ":" + nameOf(ResType::Id, entry) +
                           "' for external package '" + package + "'");
  }
  for (const SymbolDecl& s : folder.symbols) {
    const auto it = entries.find({s.type, s.name});
    if (it == entries.end()) {
      diag.error(s.source, "no definition for declared symbol " + nameOf(s.type, s.name));
      continue;
    }
    it->second.isPublic |= s.isPublic;
  }

  // ProductFilter with no product asked for: of each configuration, the value with no product
  // or "default".
  std::map<Key, std::vector<PendingValue>> kept;
  for (auto& [key, entry] : entries) {
    std::vector<PendingValue>& values = kept[key];
    std::vector<Config> done;
    for (const PendingValue& v : entry.values) {
      if (std::find(done.begin(), done.end(), v.config) != done.end()) continue;
      done.push_back(v.config);
      PendingValue* chosen = nullptr;
      int defaults = 0;
      for (PendingValue& w : entry.values) {
        if (w.config == v.config && (w.product.empty() || w.product == "default")) {
          chosen = &w;
          defaults++;
        }
      }
      if (defaults != 1) {
        diag.error(v.source,
                   std::string(defaults == 0 ? "no default product" : "multiple default products") +
                       " defined for resource " + nameOf(key.first, key.second));
        continue;
      }
      values.push_back(std::move(*chosen));
    }
    std::stable_sort(values.begin(), values.end(),
                     [](const PendingValue& a, const PendingValue& b) {
                       return compareConfigs(a.config, b.config) < 0;
                     });
  }
  if (!diag.empty()) return std::nullopt;

  // Ids: types in the order of ResType from 1, entries in the order of their names from 0.
  Package package;
  package.id = id;
  package.name = name;
  std::map<Key, ResId> ids;
  uint32_t nextEntry = 0;
  for (const auto& [key, values] : kept) {
    if (package.types.empty() || package.types.back().type != key.first) {
      package.types.push_back({key.first, static_cast<uint8_t>(package.types.size() + 1), {}});
      nextEntry = 0;
    }
    if (nextEntry > 0xffff) {
      diag.error(values.front().source,
                 "more than 65536 resources of type " + std::string(toString(key.first)));
      return std::nullopt;
    }
    ids[key] = makeResId(id, package.types.back().id, static_cast<uint16_t>(nextEntry++));
    Entry entry;
    entry.name = key.second;
    entry.isPublic = entries[key].isPublic;
    package.types.back().entries.push_back(std::move(entry));
  }

  Linker linker(package, kept, ids, deps, diag);
  for (const auto& [key, values] : kept) {
    const ResId resId = ids[key];
    Entry& entry = package.types[typeId(resId) - 1].entries[entryId(resId)];
    for (const PendingValue& v : values) {
      if (std::optional<Value> value = linker.link(v.value, v.source)) {
        entry.candidates.push_back({v.config, *value});
      }
    }
  }
  for (const ParsedXmlAttribute& attribute : folder.xmlAttributes) {
    linker.linkXmlAttribute(attribute);
  }
  if (!diag.empty()) return std::nullopt;
  return package;
}

}  // namespace hydroid::res::detail

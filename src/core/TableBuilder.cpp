#include "core/TableBuilder.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
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

// aapt2's Visibility::Level: <java-symbol> or <symbol> make a resource private, <public> public.
enum class Visibility : uint8_t { Undefined, Private, Public };

struct PendingEntry {
  std::vector<PendingValue> values;
  Visibility visibility = Visibility::Undefined;
  std::optional<ResId> id;  // the one a <public id=""> gives
  Source idSource;          // that <public>
};

// The table aapt2 compile makes of a file: of each package as written ("" before any other),
// type and name, one value for each configuration and product.
using FileTable = std::map<std::pair<std::string, Key>, PendingEntry>;

std::string nameOf(ResType type, const std::string& entry) {
  return std::string(toString(type)) + "/" + entry;
}

// `x` as printf's `format` writes it.
std::string hex(const char* format, uint32_t x) {
  char buf[16];
  std::snprintf(buf, sizeof buf, format, static_cast<unsigned>(x));
  return buf;
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
  int32_t min = std::numeric_limits<int32_t>::min();
  int32_t max = std::numeric_limits<int32_t>::max();
  std::vector<Symbol> symbols;
};

// Attribute::Print, as an error names an attribute: "(attr) enum [x=1, y=2] min=0", each symbol's
// value as the uint32_t it is. aapt2 adds " [weak]" to a weak one, which link sees none as (#81).
std::string printed(const AttrInfo& attr) {
  std::string out = "(attr) " + formatsToString(attr.formats);
  if (!attr.symbols.empty()) {
    out += " [";
    for (size_t i = 0; i < attr.symbols.size(); ++i) {
      if (i) out += ", ";
      out += attr.symbols[i].name + "=" + std::to_string(attr.symbols[i].value);
    }
    out += ']';
  }
  if (attr.min != std::numeric_limits<int32_t>::min()) out += " min=" + std::to_string(attr.min);
  if (attr.max != std::numeric_limits<int32_t>::max()) out += " max=" + std::to_string(attr.max);
  return out;
}

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
    const AttrInfo* info = nullptr;
    if (attribute.attr && linksInto(attribute.attr->package)) {
      const ResName& attr = *attribute.attr;
      const std::optional<ResId> id = resolve(attr, value.source, Naming::XmlAttribute);
      if (!id) return;
      info = attrInfo(attr, *id);
      if (!info) {
        notAnAttr(attr, value.source, Naming::XmlAttribute);
        return;
      }
    }
    switch (value.kind) {
      case ParsedItem::Kind::Reference:
      case ParsedItem::Kind::AttrRef:
        if (linksInto(value.ref.package)) resolve(value.ref, value.source);
        return;
      case ParsedItem::Kind::Raw:
        // aapt2 names the attribute here by its local name, with no package, as layout_width for
        // android:layout_width, where its "not found" gives the package (#167).
        if (info && !(info->formats & kFormatString) && !parseForAttr(value.text, *info)) {
          diag_.error(value.source, "'" + value.text + "' is incompatible with attribute " +
                                        attribute.attr->entry + " " + printed(*info));
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

  // What a reference resolve() fails to find stands for, which its error names as aapt2 does.
  enum class Naming {
    Resource,        // "resource string/s"
    XmlAttribute,    // "attribute foo": WriteAttributeName leaves out the type
    StyleAttribute,  // "style attribute 'attr/foo'", quoted
  };

  // `name` as an error names it, with no package if it is this one's.
  std::string written(const ResName& name) const {
    return (own(name.package) ? "" : name.package + ":") + name.typeName() + "/" + name.entry;
  }

  // What `naming` makes of `name` in "... not found".
  std::string described(const ResName& name, Naming naming) const {
    switch (naming) {
      case Naming::Resource: return "resource " + written(name);
      case Naming::XmlAttribute:
        return "attribute " + (own(name.package) ? "" : name.package + ":") + name.entry;
      case Naming::StyleAttribute: return "style attribute '" + written(name) + "'";
    }
    return {};
  }

  // Whether aapt2's symbol table leaves out an attr of this package that it has an id for (#175).
  // ResourceTableSymbolSource has one with a value of the default configuration and no product
  // only for an <attr>, which is always of it: <item type='attr' name='r'> in values/ it does not
  // find, by any reference, where one in values-land/ only, or of product='default', which
  // ProductFilter keeps as it is, it finds, with no <attr>.
  bool unfound(const ResName& name) const {
    if (name.type != ResType::Attr) return false;
    const std::vector<PendingValue>& values = entries_.at({ResType::Attr, name.entry});
    return std::any_of(values.begin(), values.end(), [](const PendingValue& v) {
      return v.config == Config{} && v.product.empty() &&
             !std::holds_alternative<ParsedAttr>(v.value);
    });
  }

  // An attr found where a style item or an XML attribute names an <attr>, which is not one (#166).
  // resolve() has not found any other: this one is of this package, with no value of the default
  // configuration and no product: an <item type='attr'> in values-land/ only, or an attr of
  // product='default', an <attr> or not (#178). An <attr> in values-land/ is of the default
  // configuration, where compile puts it.
  void notAnAttr(const ResName& name, const Source& source, Naming naming) {
    diag_.error(source, described(name, naming) + " is not an attribute");
  }

  std::optional<ResId> resolve(const ResName& name, const Source& source,
                               Naming naming = Naming::Resource) {
    // aapt2 looks for a macro to put its contents in place of the reference, in any package, and
    // says so when there is none.
    if (name.otherType == "macro") {
      diag_.error(source, "failed to find definition for " + written(name));
      return std::nullopt;
    }
    const std::string what = described(name, naming);
    // No package hydroid loads has a resource of a type no ResType stands for (#142), so that a
    // reference of one is not looked for. android.jar has ^attr-private, which aapt2 finds: the
    // table must keep that type before a reference finds it there (#98).
    const bool kept = name.otherType.empty();
    if (own(name.package)) {
      const auto it = ids_.find({name.type, name.entry});
      if (kept && it != ids_.end() && !unfound(name)) return it->second;
    } else if (!deps_.package(name.package)) {
      diag_.error(source, what + " not found: no package '" + name.package + "' is loaded");
      return std::nullopt;
    } else if (kept) {
      if (std::optional<ResId> id = deps_.find(name.package, name.type, name.entry)) {
        // An attr of another package that is not an <attr> aapt2 does not find, by any reference
        // (#175), before it asks whether the reference may name it: AssetManagerSymbolSource
        // finds an attr only with the bag of one.
        if (name.type == ResType::Attr && !attrInfo(name, *id)) {
          diag_.error(source, what + " not found");
          return std::nullopt;
        }
        // ReferenceLinker::IsSymbolVisible: what another package does not make <public> only a
        // private reference may name (#141).
        if (name.privateReference || deps_.entry(*id)->isPublic) return id;
        diag_.error(source, what + " is private");
        return std::nullopt;
      }
    }
    diag_.error(source, what + " not found");
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

  // The <attr> a reference of type attr names, or null if `id` is not one. Each is read once, as an
  // app's styles name the same <attr>s in many items (#92).
  const AttrInfo* attrInfo(const ResName& name, ResId id) {
    const auto [it, added] = attrInfos_.try_emplace(id);
    if (added) it->second = readAttrInfo(name, id);
    return it->second ? &*it->second : nullptr;
  }

  // Of this package, as unfound(), only the value of the default configuration and no product is
  // read: aapt2's symbol table takes an <attr> of product='default', which ProductFilter keeps as
  // it is, for none (#178).
  std::optional<AttrInfo> readAttrInfo(const ResName& name, ResId id) const {
    AttrInfo info;
    if (own(name.package)) {
      for (const PendingValue& v : entries_.at({ResType::Attr, name.entry})) {
        const auto* attr = std::get_if<ParsedAttr>(&v.value);
        if (!attr || v.config != Config{} || !v.product.empty()) continue;
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
    // RawString::Print.
    diag_.error(raw.source,
                "expected " + formatsToString(attr.formats) + " but got (raw string) " + text);
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
      const std::optional<ResId> attrId =
          resolve(item.attr, item.value.source, Naming::StyleAttribute);
      if (!attrId) {
        ok = false;
        continue;
      }
      const AttrInfo* info = attrInfo(item.attr, *attrId);
      if (!info) {
        notAnAttr(item.attr, item.value.source, Naming::StyleAttribute);
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
  // What attrInfo() has read of each <attr>, by its id: nullopt for an id that is not one.
  std::unordered_map<ResId, std::optional<AttrInfo>> attrInfos_;
};

}  // namespace

std::optional<Package> buildPackage(ParsedFolder folder, const std::string& name, uint8_t id,
                                    const ResourceTable& deps, Diagnostics& diag) {
  // The ids XML files make in other packages, by package and name, each where first made.
  std::map<std::pair<std::string, std::string>, Source> externalIds;
  // aapt2 compiles each file into a table of its own, where a weak value yields to another of the
  // file, then link merges the tables in the order of the files, where an <attr> is not weak
  // (#88). The resources of a file are together, in the order the files are read (#80), and its
  // symbols among them, as written. Compile fails before link starts, on the errors of every file
  // (#154). aapt2 adds each resource to the file's table as soon as it has read it, so the errors
  // of reading come among those of the table, where they were read (#156).
  const Diagnostics read = std::exchange(diag, Diagnostics());
  size_t e = 0;
  // Gives the errors of reading that came before the `i`th resource and the `s`th symbol.
  auto readBefore = [&](size_t i, size_t s) {
    for (; e < read.errors().size(); e++) {
      const Diagnostics::Position& at = read.positions()[e];
      if (at.resources > i || at.symbols > s) break;
      const Diagnostic& d = read.errors()[e];
      diag.error({d.file, d.line}, d.message);
    }
  };
  std::vector<FileTable> files;
  const std::filesystem::path* file = nullptr;
  std::vector<ParsedResource>& resources = folder.resources;
  for (size_t i = 0, s = 0; i <= resources.size(); i++) {
    for (; s < folder.symbols.size() && folder.symbols[s].position <= i; s++) {
      readBefore(i, s);
      const SymbolDecl& symbol = folder.symbols[s];
      if (!file || symbol.source.file != *file) files.emplace_back();
      file = &symbol.source.file;
      // AddResourcesToTable adds it as it does a resource with a value, and fails on its name in
      // the same words (#162), once for a <public type="id">, the id of which goes in with it.
      // aapt2 gives the error of a symbol with no value no file or line, as it takes them from
      // the value; it is given where the symbol is written, as is that of a second id below.
      if (!isValidEntryName(symbol.name)) {
        diag.error(symbol.source, "resource '" + nameOf(symbol.type, symbol.name) +
                                      "' has invalid entry name '" + symbol.name + "'");
        continue;
      }
      PendingEntry& entry = files.back()[{"", {symbol.type, symbol.name}}];
      entry.visibility =
          std::max(entry.visibility, symbol.isPublic ? Visibility::Public : Visibility::Private);
      if (!symbol.id) continue;
      if (entry.id && *entry.id != *symbol.id) {
        diag.error(symbol.source, "trying to add resource '" + nameOf(symbol.type, symbol.name) +
                                      "' with ID " + hex("0x%08x", *symbol.id) +
                                      " but resource already has ID " + hex("0x%08x", *entry.id));
        continue;
      }
      entry.id = symbol.id;
      entry.idSource = symbol.source;
    }
    readBefore(i, s);
    if (i == resources.size()) break;
    ParsedResource& r = resources[i];
    if (!file || r.source.file != *file) files.emplace_back();
    file = &r.source.file;
    // AddResourcesToTable: a resource whose name is not valid, or that is a duplicate, fails, and
    // its children, which follow it, are left out with no errors of their own (#159). Its errors
    // give the package written before its name unless that is this package, which the golden
    // cases take out of aapt2's words too, as compile does not know it (#161).
    const std::string resourceName =
        (r.package.empty() || r.package == name ? "" : r.package + ":") + nameOf(r.type, r.name);
    if (r.checkName && !isValidEntryName(r.name)) {
      diag.error(r.source,
                 "resource '" + resourceName + "' has invalid entry name '" + r.name + "'");
      i += r.children;
      continue;
    }
    // An id an XML file makes in another package fails link, but in android, which it only warns
    // of (#83).
    if (r.inXmlFile && !r.package.empty() && r.package != name && r.package != "android") {
      externalIds.emplace(std::pair(r.package, r.name), r.source);
    }
    PendingValue value{r.config, r.product, std::move(r.value), r.source};
    if (!merge(files.back()[{r.package, {r.type, r.name}}].values, value, Stage::Compile)) {
      diag.error(r.source, "duplicate value for resource '" + resourceName + "' with config '" +
                               toString(r.config) + "'");
      i += r.children;
    }
  }
  if (!diag.empty()) return std::nullopt;
  // Link opens the archive compile wrote before it merges anything, and fails on one that holds a
  // name twice with no other error (#158).
  for (const Diagnostic& d : folder.sameFlatName) diag.error({d.file, d.line}, d.message);
  if (!diag.empty()) return std::nullopt;

  // Every value of every entry, one for each configuration and product.
  std::map<Key, PendingEntry> entries;
  for (FileTable& table : files) {
    for (auto& [qualified, compiled] : table) {
      const auto& [package, key] = qualified;
      // aapt2 link merges only what is defined in the package being linked, written or not, after
      // compile has resolved what the file defines in each (#109): an <attr> a
      // <declare-styleable> defines in another package is left out, and the styleable then finds
      // that package's attr, or fails to. So is an id @+pkg:id/ makes, and a reference to it
      // then fails (#131).
      if (!package.empty() && package != name) continue;
      PendingEntry& entry = entries[key];
      // TableMerger's MergeEntry, before the values: the higher visibility stays, and the id with
      // public, if any; of two public ones, both ids, if they have them, must be the same. So a
      // file that makes an entry public first, without an id, keeps the id a later one gives out
      // (#154).
      if (compiled.visibility > entry.visibility) {
        if (compiled.visibility == Visibility::Public) {
          entry.id = compiled.id;
          entry.idSource = compiled.idSource;
        }
        entry.visibility = compiled.visibility;
      } else if (compiled.visibility == Visibility::Public &&
                 entry.visibility == Visibility::Public && entry.id && compiled.id &&
                 *entry.id != *compiled.id) {
        diag.error(compiled.idSource,
                   "cannot merge entry '" + key.second + "': conflicting public IDs");
        continue;
      }
      for (PendingValue& value : compiled.values) {
        if (!merge(entry.values, value, Stage::Link)) {
          diag.error(value.source, "resource '" + nameOf(key.first, key.second) +
                                       "' has a conflicting value for configuration (" +
                                       toString(value.config) + ")");
        }
      }
    }
    // aapt2 merges one file at a time and stops at the first that fails to merge, with every
    // error of that file and none of those after it (#185), before VerifyNoExternalPackages
    // (#183).
    if (!diag.empty()) return std::nullopt;
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
    if (it == entries.end() || it->second.values.empty()) {
      diag.error(s.source, "no definition for declared symbol '" + nameOf(s.type, s.name) + "'");
    }
  }
  if (!diag.empty()) return std::nullopt;

  // Every value of each entry, in the order of configuration, then product, that aapt2's
  // ResourceEntry keeps them in, and link and ProductFilter go through them in.
  std::map<Key, std::vector<PendingValue>> all;
  for (auto& [key, entry] : entries) {
    std::vector<PendingValue>& values = all[key] = std::move(entry.values);
    std::stable_sort(values.begin(), values.end(),
                     [](const PendingValue& a, const PendingValue& b) {
                       const int c = compareConfigs(a.config, b.config);
                       return c != 0 ? c < 0 : a.product < b.product;
                     });
  }

  // ProductFilter with no product asked for: of each configuration, the value with no product
  // or "default". aapt2 runs it after link, which links every value, those it leaves out too, so
  // that the errors of a reference come first (#180). Its own errors wait here till then. Of two
  // defaults, the error is of the second; of none, aapt2's has no file or line, and hydroid's is of
  // the first value.
  std::map<Key, std::vector<bool>> keep;
  Diagnostics products;
  for (const auto& [key, values] : all) {
    std::vector<bool>& kept = keep[key];
    kept.assign(values.size(), false);
    for (size_t begin = 0, end; begin < values.size(); begin = end) {
      for (end = begin + 1; end < values.size() && values[end].config == values[begin].config;) {
        end++;
      }
      std::optional<size_t> chosen;
      bool twice = false;
      for (size_t i = begin; i < end && !twice; i++) {
        if (!values[i].product.empty() && values[i].product != "default") continue;
        twice = chosen.has_value();
        if (twice) {
          products.error(values[i].source, "multiple default products defined for resource " +
                                               nameOf(key.first, key.second));
        }
        chosen = i;
      }
      if (!chosen) {
        products.error(values[begin].source,
                       "no default product defined for resource " + nameOf(key.first, key.second));
      } else if (!twice) {
        kept[*chosen] = true;
      }
    }
  }

  // Ids, as aapt2's IdAssigner gives them (#154). First those a <public id=""> gives, in the order
  // of type and name: each must be of this package, of a type id no other type has and the one of
  // its type, and of no other entry. Then the other types take the lowest type ids left from 1, in
  // the order of ResType, and the other entries of each type the lowest entry ids left from 0, in
  // the order of their names.
  std::map<Key, ResId> ids;
  std::map<ResType, uint8_t> typeIds;
  std::map<uint8_t, ResType> typeOfId;
  std::map<ResId, const Key*> given;
  for (const auto& [key, values] : all) {
    const PendingEntry& entry = entries[key];
    if (!entry.id) continue;
    const ResId resId = *entry.id;
    const std::string cannot = "can't assign ID " + hex("0x%08x", resId) + " to resource " +
                               nameOf(key.first, key.second) + " because ";
    const auto owner = typeOfId.find(typeId(resId));
    const auto has = typeIds.find(key.first);
    const auto other = given.find(resId);
    // aapt2 names the package and type id of the id where it means those the package and type
    // already have.
    if (packageId(resId) != id) {
      diag.error(entry.idSource, cannot + "package already has ID " + hex("%x", packageId(resId)));
    } else if (owner != typeOfId.end() && owner->second != key.first) {
      diag.error(entry.idSource, "can't assign type ID " + hex("0x%02x", typeId(resId)) +
                                     " to non staged type " + toString(key.first) +
                                     " because this type ID have been assigned to non staged type " +
                                     toString(owner->second));
    } else if (has != typeIds.end() && has->second != typeId(resId)) {
      diag.error(entry.idSource, cannot + "type already has ID " + hex("%x", typeId(resId)));
    } else if (other != given.end()) {
      diag.error(entry.idSource, cannot + "entry ID is already assigned to " +
                                     nameOf(other->second->first, other->second->second));
    } else {
      typeIds[key.first] = typeId(resId);
      typeOfId[typeId(resId)] = key.first;
      given[resId] = &key;
      ids[key] = resId;
      continue;
    }
    return std::nullopt;
  }
  uint8_t nextType = 1;  // fewer types than 255 take less than all ids
  for (const auto& [key, values] : all) {
    if (typeIds.count(key.first)) continue;
    while (typeOfId.count(nextType)) nextType++;
    typeIds[key.first] = nextType;
    typeOfId[nextType] = key.first;
  }
  std::map<ResType, uint32_t> nextEntry;
  for (const auto& [key, values] : all) {
    if (ids.count(key)) continue;
    const uint8_t type = typeIds[key.first];
    uint32_t& next = nextEntry[key.first];
    while (next <= 0xffff && given.count(makeResId(id, type, static_cast<uint16_t>(next)))) next++;
    if (next > 0xffff) {
      diag.error(values.front().source,
                 "more than 65536 resources of type " + std::string(toString(key.first)));
      return std::nullopt;
    }
    ids[key] = makeResId(id, type, static_cast<uint16_t>(next++));
  }

  Package package;
  package.id = id;
  package.name = name;
  for (const auto& [type, resType] : typeOfId) package.types.push_back({resType, type, {}});
  for (const auto& [key, resId] : ids) {
    Entry entry;
    entry.id = entryId(resId);
    entry.name = key.second;
    entry.isPublic = entries[key].visibility == Visibility::Public;
    const auto type = std::find_if(package.types.begin(), package.types.end(),
                                   [&](const TypeTable& t) { return t.id == typeId(resId); });
    type->entries.push_back(std::move(entry));
  }
  for (TypeTable& type : package.types) {
    std::sort(type.entries.begin(), type.entries.end(),
              [](const Entry& a, const Entry& b) { return a.id < b.id; });
  }

  Linker linker(package, all, ids, deps, diag);
  // What ProductFilter leaves out is linked into a package of its own, for its errors only: aapt2
  // flattens only what is kept, so none of it is in the table, nor its strings in the pool.
  Package left;
  left.id = id;
  left.name = name;
  Linker leftLinker(left, all, ids, deps, diag);
  for (const auto& [key, values] : all) {
    // The package is not const, only the lookup.
    Entry& entry = const_cast<Entry&>(*std::as_const(package).entry(ids[key]));
    const std::vector<bool>& kept = keep[key];
    for (size_t i = 0; i < values.size(); i++) {
      const PendingValue& v = values[i];
      if (!kept[i]) {
        leftLinker.link(v.value, v.source);
      } else if (std::optional<Value> value = linker.link(v.value, v.source)) {
        entry.candidates.push_back({v.config, *value});
      }
    }
  }
  // aapt2 link stops on "failed linking references." once the table has linked with errors, before
  // ProductFilter (#180) and before it links any XML file (#165). Every stage before has returned
  // on its errors, so these are the table's.
  if (!diag.empty()) return std::nullopt;
  for (const Diagnostic& d : products.errors()) diag.error({d.file, d.line}, d.message);
  if (!diag.empty()) return std::nullopt;
  for (const ParsedXmlAttribute& attribute : folder.xmlAttributes) {
    linker.linkXmlAttribute(attribute);
  }
  if (!diag.empty()) return std::nullopt;
  return package;
}

}  // namespace hydroid::res::detail

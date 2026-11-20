#include "core/ValuesParser.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "core/Utf8.h"
#include "core/Whitespace.h"
#include "core/Xml.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res::detail {
namespace {

int hexDigit(uint32_t c) {
  if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
  return -1;
}

// AppendCodepointToUtf8, which writes nothing for a surrogate or past U+10FFFF.
void appendCodepoint(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xc0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  } else if (cp < 0x10000) {
    if (cp >= 0xd800 && cp <= 0xdfff) return;
    out += static_cast<char>(0xe0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  } else if (cp <= 0x10ffff) {
    out += static_cast<char>(0xf0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  }
}

// aapt2's ResourceUtils::StringBuilder: the text and spans of a string, with span positions in
// UTF-16 units. Each piece of text collapses its own whitespace, and a span starting or ending
// also ends a "quote".
class StringBuilder {
 public:
  void appendText(std::string_view text) {
    if (!error_.empty()) return;
    const size_t previous = text_.size();
    Utf8Reader in(text);
    while (in.hasNext()) {
      uint32_t c = in.next();
      if (!quote_ && isSpace(c)) {
        if (!lastWasSpace_) {
          text_ += ' ';
          lastWasSpace_ = true;
        }
        continue;
      }
      lastWasSpace_ = false;
      if (c == '\\') {
        if (!in.hasNext()) continue;  // a \ at the end is dropped
        c = in.next();
        switch (c) {
          case 't': text_ += '\t'; break;
          case 'n': text_ += '\n'; break;
          case 'u': {
            // Up to four hex digits, as many as the text has.
            uint32_t code = 0;
            for (int i = 0; i < 4 && in.hasNext(); i++) {
              const int d = hexDigit(in.next());
              if (d < 0) {
                error_ = "invalid unicode escape sequence in string\n\"" + std::string(text) + '"';
                return;
              }
              code = code * 16 + static_cast<uint32_t>(d);
            }
            appendCodepoint(code, text_);
            break;
          }
          default:
            // \#, \@, \?, \", \', \\ and any other: the character itself.
            appendCodepoint(c, text_);
            break;
        }
      } else if (c == '"') {
        quote_ = !quote_;
      } else if (!quote_ && c == '\'') {
        // As aapt2 puts it, and a bad \u above, the text on a line of its own.
        error_ = "unescaped apostrophe in string\n\"" + std::string(text) + '"';
        return;
      } else {
        appendCodepoint(c, text_);
      }
    }
    if (in.invalid()) {
      error_ = "invalid unicode code point in string";
      return;
    }
    Utf8Reader added(std::string_view(text_).substr(previous));
    while (added.hasNext()) utf16Length_ += added.next() >= 0x10000 ? 2 : 1;
  }

  size_t startSpan(std::string name) {
    if (!error_.empty()) return 0;
    resetTextState();
    spans_.push_back({std::move(name), utf16Length_, utf16Length_});
    return spans_.size() - 1;
  }

  void endSpan(size_t span) {
    if (!error_.empty()) return;
    resetTextState();
    spans_[span].lastChar = utf16Length_ - 1;  // inclusive, so an empty span ends before it starts
  }

  const std::string& error() const { return error_; }
  std::string& text() { return text_; }
  std::vector<Span>& spans() { return spans_; }

 private:
  void resetTextState() {
    quote_ = false;
    lastWasSpace_ = false;
  }

  std::string text_;
  std::vector<Span> spans_;
  uint32_t utf16Length_ = 0;
  bool quote_ = false;
  bool lastWasSpace_ = false;
  std::string error_;
};

// The XML under a value element as ResourceParser::FlattenXmlSubtree sees it: text, tags with no
// namespace (spans), <xliff:g> (untranslatable, but kept) and other tags, whose text is kept.
struct FlatNode {
  enum class Kind : uint8_t { Plain, Segment, Span, Untranslatable };
  Kind kind = Kind::Plain;
  std::string data;  // Segment: the text; Span: the tag and its attributes
  std::vector<std::unique_ptr<FlatNode>> children;
};

// A value element's text as aapt2 reads it: `raw` is its text and that of its tags, as written,
// to read a literal or reference from; `styled` is what StringBuilder makes of it.
struct Flattened {
  std::string raw;
  StyledString styled;
};

// ResourceUtils::ParseBool's words, as in parseBool.
std::optional<bool> boolOf(std::string_view s) {
  if (std::optional<Value> v = parseBool(s)) return v->boolValue();
  return std::nullopt;
}

// util::VerifyJavaStringFormat: false if a string has more than one argument and any of them is
// not positional (%s %s, %1$s %s), as translations may reorder them.
bool verifyJavaStringFormat(std::string_view s) {
  size_t argCount = 0;
  bool nonPositional = false;
  const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '%' && i + 1 < s.size()) {
      i++;
      if (s[i] == '%' || s[i] == 'n') {
        i++;
        continue;
      }
      argCount++;
      size_t digits = 0;
      while (i + digits < s.size() && isDigit(s[i + digits])) digits++;
      if (digits > 0) {
        i += digits;
        if (i < s.size() && s[i] != '$') nonPositional = true;  // a width, not a position
      } else if (s[i] == '<') {
        nonPositional = true;  // the last argument again
        i++;
        if (i < s.size() && s[i] == '$') i++;
      } else {
        nonPositional = true;
      }
      while (i < s.size() && (s[i] == '-' || s[i] == '#' || s[i] == '+' || s[i] == ' ' ||
                              s[i] == ',' || s[i] == '(' || isDigit(s[i]))) {
        i++;
      }
      // A conversion only Time.format() has: not a String.format() string at all.
      if (i < s.size() && std::string_view("DFKMWZkmwyz").find(s[i]) != std::string_view::npos) {
        return true;
      }
    }
    if (i < s.size()) i++;
  }
  return !(argCount > 1 && nonPositional);
}

// ParseFormatTypeNoEnumsOrFlags.
uint32_t formatOf(std::string_view s) {
  if (s == "reference") return kFormatReference;
  if (s == "string") return kFormatString;
  if (s == "integer") return kFormatInteger;
  if (s == "boolean") return kFormatBoolean;
  if (s == "color") return kFormatColor;
  if (s == "float") return kFormatFloat;
  if (s == "dimension") return kFormatDimension;
  if (s == "fraction") return kFormatFraction;
  return 0;
}

// ParseFormatAttribute: formats joined by '|', each with whitespace around; 0 if one is not a
// format.
uint32_t formatsOf(std::string_view s) {
  uint32_t mask = 0;
  for (;;) {
    const size_t bar = s.find('|');
    const std::string_view part = trim(s.substr(0, bar));
    uint32_t format = part == "enum"    ? kFormatEnum
                      : part == "flags" ? kFormatFlags
                                        : formatOf(part);
    if (format == 0) return 0;
    mask |= format;
    if (bar == std::string_view::npos) return mask;
    s.remove_prefix(bar + 1);
  }
}

// The value of attribute `name` with no namespace, as written.
std::optional<std::string_view> attribute(pugi::xml_node el, const char* name) {
  if (pugi::xml_attribute a = el.attribute(name)) return std::string_view(a.value());
  return std::nullopt;
}

// xml::FindNonEmptyAttribute: the value without whitespace around, if any is left.
std::optional<std::string_view> nonEmptyAttribute(pugi::xml_node el, const char* name) {
  if (std::optional<std::string_view> v = attribute(el, name)) {
    if (const std::string_view t = trim(*v); !t.empty()) return t;
  }
  return std::nullopt;
}

// ResourceUtils::ExtractResourceName: [package:]type/entry, where the first ':' before an entry
// ends the package and the first '/' the type, in either order.
void extractResourceName(std::string_view s, std::string_view* package, std::string_view* type,
                         std::string_view* entry) {
  if (!s.empty() && s[0] == '@') s.remove_prefix(1);
  size_t start = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (type->empty() && s[i] == '/') {
      *type = s.substr(start, i - start);
      start = i + 1;
    } else if (package->empty() && s[i] == ':') {
      *package = s.substr(start, i - start);
      start = i + 1;
    }
  }
  *entry = s.substr(start);
}

const struct {
  std::string_view element;
  ResType type;
  uint32_t formats;
} kItemElements[] = {
    {"bool", ResType::Bool, kFormatBoolean},
    {"color", ResType::Color, kFormatColor},
    {"dimen", ResType::Dimen, kFormatFloat | kFormatFraction | kFormatDimension},
    {"drawable", ResType::Drawable, kFormatColor},
    {"fraction", ResType::Fraction, kFormatFloat | kFormatFraction | kFormatDimension},
    {"integer", ResType::Integer, kFormatInteger},
    {"string", ResType::String, kFormatString},
};

// The types of aapt2 that a reference may name and no ResType stands for (#142). aapt2 has
// overlayable too, but does not read it in a reference: @overlayable/o is text.
constexpr std::string_view kOtherRefTypes[] = {"^attr-private", "configVarying", "macro"};

// Elements aapt2 reads that this does not yet.
constexpr std::string_view kUnsupported[] = {
    "add-resource",
    "configVarying",
    "macro",
    "overlayable",
    "public-group",
    "staging-public-group",
    "staging-public-group-final",
};

class Parser {
 public:
  Parser(const XmlDocument& doc, const std::filesystem::path& file, const Config& config,
         ParsedFolder& out, Diagnostics& diag)
      : doc_(doc), file_(file), config_(config), out_(out), diag_(diag) {}

  void parseResources(pugi::xml_node resources) {
    for (pugi::xml_node node : resources.children()) {
      if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
        if (!trim(node.value()).empty()) error(node, "plain text not allowed here");
        continue;
      }
      if (node.type() != pugi::node_element) continue;
      if (!elementNamespace(node).empty()) continue;  // another namespace: skipped
      const std::string_view name = node.name();
      if (name == "skip" || name == "eat-comment") continue;

      ParsedResource res;
      res.config = config_;
      res.source = at(node);
      if (std::optional<std::string_view> product = nonEmptyAttribute(node, "product")) {
        res.product = std::string(*product);
      }
      children_.clear();
      if (!parseResource(node, res) || !add(std::move(res))) continue;
      for (ParsedResource& child : children_) add(std::move(child));
    }
  }

 private:
  Source at(pugi::xml_node node) const { return {file_, doc_.line(node)}; }
  void error(pugi::xml_node node, std::string message) {
    diag_.error(at(node), std::move(message));
  }

  // AddResourcesToTable's check of the name. False if it fails.
  bool add(ParsedResource res) {
    if (!isValidEntryName(res.name)) {
      diag_.error(res.source, "resource '" + std::string(toString(res.type)) + "/" + res.name +
                                  "' has invalid entry name '" + res.name + "'");
      return false;
    }
    out_.resources.push_back(std::move(res));
    return true;
  }

  // An id that @+[package:]id/ makes, its name checked as any resource's. aapt2's ParseXml adds it
  // to the table as soon as it reads it, before the resource it is in, and whether that one is
  // read or not.
  void addId(const std::string& name, const Source& source, const std::string& package = "") {
    ParsedResource id = makeIdResource(name, source);
    id.package = package;
    add(std::move(id));
  }

  bool parseResource(pugi::xml_node el, ParsedResource& res) {
    const std::string_view element = el.name();
    std::string type(element);
    uint32_t formats = 0;
    bool canBeItem = true;
    bool canBeBag = true;
    if (element == "item") {
      canBeBag = false;
      formats = kFormatAny;
      std::optional<std::string_view> itemType = nonEmptyAttribute(el, "type");
      if (!itemType) {
        error(el, "<item> must have a 'type' attribute");
        return false;
      }
      type = std::string(*itemType);
      if (std::optional<std::string_view> format = nonEmptyAttribute(el, "format")) {
        formats = formatOf(*format);
        if (formats == 0) {
          error(el, "'" + std::string(*format) + "' is an invalid format");
          return false;
        }
      }
    } else if (element == "bag") {
      // A bag of the type it names: <bag type="style"> is a <style>.
      canBeItem = false;
      std::optional<std::string_view> bagType = nonEmptyAttribute(el, "type");
      if (!bagType) {
        error(el, "<bag> must have a 'type' attribute");
        return false;
      }
      type = std::string(*bagType);
    }

    const std::optional<std::string_view> name = nonEmptyAttribute(el, "name");
    auto needName = [&]() {
      if (name) {
        res.name = std::string(*name);
        return true;
      }
      error(el, "<" + std::string(element) + "> missing 'name' attribute");
      return false;
    };

    if (type == "id") {
      if (!needName()) return false;
      res.type = ResType::Id;
      if (!parseItem(el, res, formats)) return false;
      ParsedItem& item = std::get<ParsedItem>(res.value);
      const bool empty =
          (item.kind == ParsedItem::Kind::String && item.text.empty()) ||
          (item.kind == ParsedItem::Kind::Literal && item.literal.kind() == ValueKind::Null);
      if (empty) {
        item.kind = ParsedItem::Kind::Id;
      } else if (item.kind != ParsedItem::Kind::Reference || item.ref.type != ResType::Id) {
        error(el, "<" + std::string(element) +
                      "> inner element must either be a resource reference or empty");
        return false;
      }
      return true;
    }
    for (std::string_view unsupported : kUnsupported) {
      if (type == unsupported) {
        error(el, "<" + type + "> is not supported yet");
        return false;
      }
    }

    if (canBeItem) {
      for (const auto& item : kItemElements) {
        if (type != item.element) continue;
        if (!needName()) return false;
        res.type = item.type;
        return parseItem(el, res, formats != 0 ? formats : item.formats);
      }
    }
    if (canBeBag) {
      if (type == "array") return needName() && parseArray(el, res);
      if (type == "integer-array") return needName() && parseArray(el, res, kFormatInteger);
      if (type == "string-array") return needName() && parseArray(el, res, kFormatString);
      if (type == "attr") return needName() && parseAttr(el, res, /*weak=*/false);
      if (type == "declare-styleable") return needName() && parseDeclareStyleable(el, res);
      if (type == "plurals") return needName() && parsePlurals(el, res);
      if (type == "style") return needName() && parseStyle(el, res);
      if (type == "public") return needName() && parsePublic(el, res);
      if (type == "java-symbol" || type == "symbol") return needName() && parseSymbol(el, res);
    }
    if (canBeItem) {
      // <item type="layout"> and the like can only be references.
      if (std::optional<ResType> refType = parseResType(type)) {
        if (!needName()) return false;
        res.type = *refType;
        std::optional<ParsedItem> item = parseXml(el, kFormatReference, /*allowRaw=*/false);
        if (!item) {
          error(el, "invalid value for type '" + type + "'. Expected a reference");
          return false;
        }
        res.value = std::move(*item);
        return true;
      }
    }
    error(el, "unknown resource type '" + type + "'");
    return false;
  }

  // ParseItem, and ParseString for a string.
  bool parseItem(pugi::xml_node el, ParsedResource& res, uint32_t formats) {
    if (formats == kFormatString) return parseString(el, res);
    std::optional<ParsedItem> item = parseXml(el, formats, /*allowRaw=*/false);
    if (!item) {
      error(el, "invalid " + std::string(toString(res.type)));
      return false;
    }
    res.value = std::move(*item);
    return true;
  }

  bool parseString(pugi::xml_node el, ParsedResource& res) {
    bool formatted = true;
    bool translatable = true;
    if (std::optional<std::string_view> v = attribute(el, "formatted")) {
      std::optional<bool> b = boolOf(*v);
      if (!b) {
        error(el, "invalid value for 'formatted'. Must be a boolean");
        return false;
      }
      formatted = *b;
    }
    if (std::optional<std::string_view> v = attribute(el, "translatable")) {
      std::optional<bool> b = boolOf(*v);
      if (!b) {
        error(el, "invalid value for 'translatable'. Must be a boolean");
        return false;
      }
      translatable = *b;
    }
    std::optional<ParsedItem> item = parseXml(el, kFormatString, /*allowRaw=*/false);
    if (!item) {
      error(el, "not a valid string");
      return false;
    }
    if (item->kind == ParsedItem::Kind::String && formatted && translatable &&
        !verifyJavaStringFormat(item->text)) {
      error(el,
            "multiple substitutions specified in non-positional format; did you mean to add "
            "the formatted=\"false\" attribute?");
      return false;
    }
    res.value = std::move(*item);
    return true;
  }

  // ResourceParser::ParseXml: a value element as a styled string if it has tags, else as a
  // literal or reference `formats` allows, else as a string if they allow one, else (`allowRaw`)
  // as text to read later, else as @null if there is no text. nullopt if none of these.
  std::optional<ParsedItem> parseXml(pugi::xml_node el, uint32_t formats, bool allowRaw) {
    std::optional<Flattened> flat = flatten(el);
    if (!flat) return std::nullopt;
    ParsedItem item;
    item.source = at(el);
    if (!flat->styled.spans.empty()) {
      item.kind = ParsedItem::Kind::StyledString;
      item.styled = std::move(flat->styled);
      return item;
    }
    if (tryParseItem(el, flat->raw, formats, item)) return item;
    if (formats & kFormatString) {
      item.kind = ParsedItem::Kind::String;
      item.text = std::move(flat->styled.text);
      return item;
    }
    if (allowRaw) {
      item.kind = ParsedItem::Kind::Raw;
      item.text = std::string(trim(flat->raw));
      return item;
    }
    if (trim(flat->raw).empty()) {
      item.literal = Value::null();
      return item;
    }
    return std::nullopt;
  }

  // ResourceUtils::TryParseItemForAttribute by format: @null, @empty, a reference, then the
  // literals `formats` allows.
  bool tryParseItem(pugi::xml_node el, std::string_view raw, uint32_t formats, ParsedItem& item) {
    const std::string_view t = trim(raw);
    if (t == "@null" || t == "@empty") {
      item.literal = t == "@null" ? Value::null() : Value::empty();
      return true;
    }
    std::optional<ParsedReference> ref = parseReference(raw);
    // @+id/ of a name no id can have is not a reference: <string>@+id/1x</string> is that text.
    if (ref && ref->create && !isValidEntryName(ref->name.entry)) ref.reset();
    if (ref) {
      // The id is in the package as written, not resolved as the reference is, as
      // ResourceParser::ParseXml makes it, which the table builder keeps only in that package
      // (#131).
      if (ref->create) addId(ref->name.entry, item.source, ref->name.package);
      item.kind = ref->attr ? ParsedItem::Kind::AttrRef : ParsedItem::Kind::Reference;
      item.ref = std::move(ref->name);
      resolvePackage(el, item.ref);
      return true;
    }
    if (std::optional<Value> v = parseLiteral(raw, formats)) {
      item.literal = *v;
      return true;
    }
    return false;
  }

  std::optional<Flattened> flatten(pugi::xml_node el) {
    Flattened out;
    FlatNode root;
    std::string current;
    FlatNode* first = nullptr;
    FlatNode* last = nullptr;
    bool sawSpan = false;
    bool inXliffG = false;
    bool failed = false;

    auto flush = [&](FlatNode& parent) {
      if (current.empty()) return;
      auto segment = std::make_unique<FlatNode>();
      segment->kind = FlatNode::Kind::Segment;
      segment->data = std::move(current);
      current.clear();
      last = segment.get();
      if (!first) first = last;
      parent.children.push_back(std::move(segment));
    };
    auto walk = [&](auto& self, pugi::xml_node node, FlatNode& parent) -> void {
      for (pugi::xml_node child : node.children()) {
        if (failed) return;
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
          current += child.value();
          out.raw += child.value();
          continue;
        }
        if (child.type() != pugi::node_element) continue;
        flush(parent);
        const std::string ns = elementNamespace(child);
        auto inner = std::make_unique<FlatNode>();
        const bool xliffG = ns == kXliffNamespace && localName(child.name()) == "g";
        if (ns.empty()) {
          // An HTML tag, which is a span named for it and its attributes.
          inner->kind = FlatNode::Kind::Span;
          inner->data = child.name();
          for (pugi::xml_attribute a : child.attributes()) {
            if (isNamespaceDeclaration(a)) continue;
            inner->data += ';';
            inner->data += localName(a.name());
            inner->data += '=';
            inner->data += a.value();
          }
          sawSpan = true;
        } else if (xliffG) {
          if (inXliffG) {
            error(child, "illegal nested XLIFF 'g' tag");
            failed = true;
            return;
          }
          inXliffG = true;
          inner->kind = FlatNode::Kind::Untranslatable;
        }
        // Any other tag (another XLIFF tag, or another namespace) only keeps its text.
        FlatNode& added = *parent.children.emplace_back(std::move(inner));
        self(self, child, added);
        flush(added);
        if (xliffG) inXliffG = false;
      }
    };
    walk(walk, el, root);
    if (failed) return std::nullopt;
    flush(root);

    // With no tags, the first piece of text loses its leading whitespace and the last its
    // trailing whitespace, as written: before \-escapes and "quotes" are read.
    if (!sawSpan) {
      if (first) first->data = std::string(trimLeading(first->data));
      if (last) last->data = std::string(trimTrailing(last->data));
    }

    StringBuilder builder;
    auto build = [&](auto& self, const FlatNode& node) -> void {
      for (const std::unique_ptr<FlatNode>& child : node.children) {
        switch (child->kind) {
          case FlatNode::Kind::Segment: builder.appendText(child->data); break;
          case FlatNode::Kind::Span: {
            const size_t span = builder.startSpan(child->data);
            self(self, *child);
            builder.endSpan(span);
            break;
          }
          case FlatNode::Kind::Plain:
          case FlatNode::Kind::Untranslatable: self(self, *child); break;
        }
      }
    };
    build(build, root);
    if (!builder.error().empty()) {
      error(el, builder.error());
      return std::nullopt;
    }
    out.styled.text = std::move(builder.text());
    out.styled.spans = std::move(builder.spans());
    return out;
  }

  // The elements of a bag that hold its items: other elements fail it, but for <skip> and
  // <eat-comment>; text and comments are ignored.
  template <typename Fn>
  bool forEachItem(pugi::xml_node el, const char* itemName, Fn&& fn) {
    bool ok = true;
    for (pugi::xml_node child : el.children()) {
      if (child.type() != pugi::node_element) continue;
      const bool plain = elementNamespace(child).empty();
      const std::string_view name = child.name();
      if (plain && name == itemName) {
        ok &= fn(child);
      } else if (!(plain && (name == "skip" || name == "eat-comment"))) {
        error(child, "unknown tag <" + std::string(name) + "> in <" + el.name() + ">");
        ok = false;
      }
    }
    return ok;
  }

  bool parseArray(pugi::xml_node el, ParsedResource& res, uint32_t formats = 0) {
    res.type = ResType::Array;
    if (formats == 0) {
      formats = kFormatAny;
      if (std::optional<std::string_view> format = nonEmptyAttribute(el, "format")) {
        formats = formatOf(*format);
        if (formats == 0) {
          error(el, "'" + std::string(*format) + "' is an invalid format");
          return false;
        }
      }
    }
    if (std::optional<std::string_view> v = attribute(el, "translatable"); v && !boolOf(*v)) {
      error(el, "invalid value for 'translatable'. Must be a boolean");
      return false;
    }
    ParsedArray array;
    const bool ok = forEachItem(el, "item", [&](pugi::xml_node item) {
      std::optional<ParsedItem> value = parseXml(item, formats, /*allowRaw=*/false);
      if (!value) {
        error(item, "could not parse array item");
        return false;
      }
      array.items.push_back(std::move(*value));
      return true;
    });
    res.value = std::move(array);
    return ok;
  }

  bool parsePlurals(pugi::xml_node el, ParsedResource& res) {
    res.type = ResType::Plurals;
    static constexpr std::string_view kQuantities[] = {"zero", "one",  "two",
                                                       "few",  "many", "other"};
    ParsedPlurals plurals;
    const bool ok = forEachItem(el, "item", [&](pugi::xml_node item) {
      std::optional<std::string_view> quantity = nonEmptyAttribute(item, "quantity");
      if (!quantity) {
        error(item, "<item> in <plurals> requires attribute 'quantity'");
        return false;
      }
      size_t index = 0;
      while (index < 6 && kQuantities[index] != *quantity) index++;
      if (index == 6) {
        error(item, "<item> in <plural> has invalid value '" + std::string(*quantity) +
                        "' for attribute 'quantity'");
        return false;
      }
      if (plurals.items[index]) {
        error(item, "duplicate quantity '" + std::string(*quantity) + "'");
        return false;
      }
      plurals.items[index] = parseXml(item, kFormatString, /*allowRaw=*/false);
      return plurals.items[index].has_value();
    });
    res.value = std::move(plurals);
    return ok;
  }

  bool parseStyle(pugi::xml_node el, ParsedResource& res) {
    res.type = ResType::Style;
    ParsedStyle style;
    if (std::optional<std::string_view> parent = attribute(el, "parent")) {
      // parent="" is no parent, nor is one inferred then; aapt2 takes parent=" " for it too.
      parent = trim(*parent);
      if (!parent->empty()) {
        std::string why;
        style.parent = parseStyleParent(*parent, &why);
        if (!style.parent) {
          error(el, why);
          return false;
        }
        resolvePackage(el, *style.parent);
      }
    } else if (const size_t dot = res.name.rfind('.'); dot != std::string::npos) {
      style.parent = ResName{"", ResType::Style, res.name.substr(0, dot)};
    }
    const bool ok = forEachItem(el, "item", [&](pugi::xml_node item) {
      std::optional<std::string_view> name = nonEmptyAttribute(item, "name");
      if (!name) {
        error(item, "<item> must have a 'name' attribute");
        return false;
      }
      ResName attr = attributeName(item, *name);
      std::optional<ParsedItem> value = parseXml(item, 0, /*allowRaw=*/true);
      if (!value) {
        error(item, "could not parse style item");
        return false;
      }
      style.items.push_back({std::move(attr), std::move(*value)});
      return true;
    });
    res.value = std::move(style);
    return ok;
  }

  // ResourceUtils::ParseStyleParentReference: [@|?][*][package:][style/]entry, where a type
  // needs the @ or ? or a package.
  static std::optional<ResName> parseStyleParent(std::string_view s, std::string* why) {
    std::string_view name = s;
    bool leading = false;
    if (name[0] == '@' || name[0] == '?') {
      leading = true;
      name.remove_prefix(1);
    }
    const bool priv = !name.empty() && name[0] == '*';
    if (priv) name.remove_prefix(1);
    std::string_view package;
    std::string_view type;
    std::string_view entry;
    extractResourceName(name, &package, &type, &entry);
    if (!type.empty() && parseResType(type) != ResType::Style) {
      *why = "invalid resource type '" + std::string(type) + "' for parent of style";
      return std::nullopt;
    }
    if (!leading && package.empty() && !type.empty()) {
      *why = "invalid parent reference '" + std::string(s) + "'";
      return std::nullopt;
    }
    ResName parent{std::string(package), ResType::Style, std::string(entry)};
    parent.privateReference = priv;
    return parent;
  }

  // ResourceUtils::ParseXmlAttributeName: [*][package:]entry, an attr, where * only marks a
  // private reference. Without a package, or with nothing after its ':', the entry is the whole
  // trimmed text, * and all: aapt2 then looks for attr/*n or com.probe:attr/com.probe: (#84).
  static ResName attributeName(pugi::xml_node el, std::string_view s) {
    const std::string_view trimmed = trim(s);
    s = trimmed;
    const bool priv = !s.empty() && s[0] == '*';
    if (priv) s.remove_prefix(1);
    ResName name{"", ResType::Attr, std::string(trimmed)};
    name.privateReference = priv;
    if (const size_t colon = s.find(':'); colon != std::string_view::npos) {
      name.package = std::string(s.substr(0, colon));
      resolvePackage(el, name);
      if (colon + 1 < s.size()) name.entry = std::string(s.substr(colon + 1));
    }
    return name;
  }

  bool parseAttr(pugi::xml_node el, ParsedResource& res, bool weak) {
    res.type = ResType::Attr;
    res.config = Config{};  // aapt2 warns and keeps an <attr> in the default configuration only
    ParsedAttr attr;
    attr.weak = weak;
    uint32_t formats = 0;
    if (std::optional<std::string_view> format = attribute(el, "format")) {
      formats = formatsOf(*format);
      if (formats == 0) {
        error(el, "invalid attribute format '" + std::string(*format) + "'");
        return false;
      }
    }
    std::optional<int32_t> limits[2];
    const char* const kLimits[] = {"min", "max"};
    for (int i = 0; i < 2; i++) {
      std::optional<std::string_view> v = attribute(el, kLimits[i]);
      if (!v) continue;
      const std::string_view t = trim(*v);
      if (std::optional<Value> n = t.empty() ? std::nullopt : parseInteger(t)) {
        limits[i] = n->intValue();
      } else {
        error(el, "invalid '" + std::string(kLimits[i]) + "' value '" + std::string(t) + "'");
        return false;
      }
    }
    if ((limits[0] || limits[1]) && !(formats & kFormatInteger)) {
      error(el, "'min' and 'max' can only be used when format='integer'");
      return false;
    }
    if (limits[0]) attr.min = *limits[0];
    if (limits[1]) attr.max = *limits[1];

    bool ok = true;
    for (pugi::xml_node child : el.children()) {
      if (child.type() != pugi::node_element) continue;
      const std::string_view name = child.name();
      const bool plain = elementNamespace(child).empty();
      if (plain && (name == "enum" || name == "flag")) {
        const bool isEnum = name == "enum";
        if (formats & (isEnum ? kFormatFlags : kFormatEnum)) {
          error(child, isEnum ? "can not define an <enum>; already defined a <flag>"
                              : "can not define a <flag>; already defined an <enum>");
          ok = false;
          continue;
        }
        formats |= isEnum ? kFormatEnum : kFormatFlags;
        std::optional<std::string_view> symbol = nonEmptyAttribute(child, "name");
        std::optional<std::string_view> value = nonEmptyAttribute(child, "value");
        std::optional<Value> number = value ? parseInteger(*value) : std::nullopt;
        if (!symbol || !value || !number) {
          error(child, !symbol  ? "no attribute 'name' found for tag <" + std::string(name) + ">"
                       : !value ? "no attribute 'value' found for tag <" + std::string(name) + ">"
                                : "invalid value '" + std::string(*value) + "' for <" +
                                      std::string(name) + ">; must be an integer");
          ok = false;
          continue;
        }
        ParsedAttr::Symbol s{std::string(*symbol), static_cast<uint32_t>(number->intValue()),
                             number->intFormat(), at(child)};
        auto pos = attr.symbols.begin();
        while (pos != attr.symbols.end() && pos->name < s.name) ++pos;
        if (pos != attr.symbols.end() && pos->name == s.name) {
          error(child, "duplicate symbol '" + s.name + "'");
          ok = false;
          continue;
        }
        children_.push_back(makeIdResource(s.name, s.source));
        attr.symbols.insert(pos, std::move(s));
      } else if (!(plain && (name == "skip" || name == "eat-comment"))) {
        error(child, "unknown tag <" + std::string(name) + "> in <attr>");
        ok = false;
      }
    }
    if (!ok) return false;
    attr.formats = formats != 0 ? formats : kFormatAny;
    res.value = std::move(attr);
    return true;
  }

  bool parseDeclareStyleable(pugi::xml_node el, ParsedResource& res) {
    res.type = ResType::Styleable;
    res.config = Config{};  // as for <attr>
    ParsedStyleable styleable;
    const bool ok = forEachItem(el, "attr", [&](pugi::xml_node child) {
      std::optional<std::string_view> name = nonEmptyAttribute(child, "name");
      if (!name) {
        error(child, "<attr> tag must have a 'name' attribute");
        return false;
      }
      ResName ref = attributeName(child, *name);
      ParsedResource attr;
      attr.name = ref.entry;
      attr.package = ref.package;
      attr.source = at(child);
      const size_t ids = children_.size();
      if (!parseAttr(child, attr, /*weak=*/true)) return false;
      // An <attr> with no format only refers to one defined elsewhere. One with a package is
      // defined in that package, as ParseDeclareStyleable names it, which the table builder sees
      // is the one being loaded (#109). It comes before the ids of its <enum>s and <flag>s.
      if (std::get<ParsedAttr>(attr.value).formats != kFormatAny) {
        children_.insert(children_.begin() + static_cast<ptrdiff_t>(ids), std::move(attr));
      }
      styleable.attrs.push_back(std::move(ref));
      return true;
    });
    res.value = std::move(styleable);
    return ok;
  }

  bool parsePublic(pugi::xml_node el, ParsedResource& res) {
    std::optional<std::string_view> type = nonEmptyAttribute(el, "type");
    if (!type) {
      error(el, "<public> must have a 'type' attribute");
      return false;
    }
    std::optional<ResType> t = parseResType(*type);
    if (!t) {
      error(el, "invalid resource type '" + std::string(*type) + "' in <public>");
      return false;
    }
    // ResourceUtils::ParseResourceId: hex, as ResTable::stringToInt reads it, of a type other than
    // 0. The package is checked when ids are given, as aapt2 link does (#154).
    std::optional<ResId> id;
    if (std::optional<std::string_view> text = nonEmptyAttribute(el, "id")) {
      std::optional<Value> v = parseInteger(*text);
      if (!v || v->intFormat() != IntFormat::Hex ||
          typeId(static_cast<ResId>(v->intValue())) == 0) {
        error(el, "invalid resource ID '" + std::string(*text) + "' in <public>");
        return false;
      }
      id = static_cast<ResId>(v->intValue());
    }
    out_.symbols.push_back(
        {*t, res.name, /*isPublic=*/true, id, res.source, out_.resources.size()});
    // A public id is also its definition, which aapt2 keeps in the configuration of the folder,
    // as it does a value of any resource there, and only warns of the <public> (#82). It has the
    // product of the <public> (#136).
    if (*t == ResType::Id) add(makeIdResource(res.name, res.source, config_, res.product));
    return false;  // nothing more to add
  }

  bool parseSymbol(pugi::xml_node el, const ParsedResource& res) {
    std::optional<std::string_view> type = nonEmptyAttribute(el, "type");
    if (!type) {
      error(el, "<" + std::string(el.name()) + "> must have a 'type' attribute");
      return false;
    }
    std::optional<ResType> t = parseResType(*type);
    if (!t) {
      error(el, "invalid resource type '" + std::string(*type) + "' in <" + el.name() + ">");
      return false;
    }
    // It marks the resource private, which no table or dump shows, but it must exist.
    out_.symbols.push_back(
        {*t, res.name, /*isPublic=*/false, std::nullopt, res.source, out_.resources.size()});
    return false;
  }

  const XmlDocument& doc_;
  const std::filesystem::path& file_;
  const Config& config_;
  ParsedFolder& out_;
  Diagnostics& diag_;
  // aapt2's child_resources of the element being read: the <attr>s of a <declare-styleable> and the
  // ids of <enum>s and <flag>s, each <attr> before its ids. AddResourcesToTable adds them after
  // the element's resource, and none of them if it is not read or its name is not valid (#156).
  std::vector<ParsedResource> children_;
};

}  // namespace

bool isValidEntryName(std::string_view name) {
  if (name.empty() || (name[0] >= '0' && name[0] <= '9')) return false;
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '.' || c == '-' || static_cast<unsigned char>(c) >= 0x80;
    if (!ok) return false;
  }
  return true;
}

ParsedResource makeIdResource(std::string name, const Source& source, const Config& config,
                              std::string product) {
  ParsedResource id;
  id.type = ResType::Id;
  id.name = std::move(name);
  id.config = config;
  id.product = std::move(product);
  ParsedItem item;
  item.kind = ParsedItem::Kind::Id;
  item.source = source;
  id.value = std::move(item);
  id.source = source;
  return id;
}

void parseValues(std::string_view xml, const std::filesystem::path& file, const Config& config,
                 ParsedFolder& out, Diagnostics& diag) {
  XmlDocument doc;
  std::string why;
  int line = 0;
  if (!doc.load(xml, XmlExtent::RootElement, &why, &line)) {
    diag.error({file, line}, "xml parser error: " + why);
    return;
  }
  const pugi::xml_node root = doc.root();
  if (!elementNamespace(root).empty() || std::string_view(root.name()) != "resources") {
    diag.error({file, doc.line(root)}, "root element must be <resources>");
    return;
  }
  Parser(doc, file, config, out, diag).parseResources(root);
}

std::optional<ParsedReference> parseReference(std::string_view text) {
  const std::string_view t = trim(text);
  if (t.empty()) return std::nullopt;
  ParsedReference ref;
  std::string_view package;
  std::string_view type;
  std::string_view entry;
  if (t[0] == '@') {
    std::string_view rest = t.substr(1);
    if (!rest.empty() && rest[0] == '+') {
      ref.create = true;
      rest.remove_prefix(1);
    }
    if (rest.empty()) return std::nullopt;
    const bool priv = rest[0] == '*';
    if (priv) rest.remove_prefix(1);
    extractResourceName(rest, &package, &type, &entry);
    const std::optional<ResType> parsed = type.empty() ? std::nullopt : parseResType(type);
    std::string_view otherType;
    if (!parsed) {
      for (std::string_view other : kOtherRefTypes) {
        if (type == other) otherType = other;
      }
    }
    if ((!parsed && otherType.empty()) || entry.empty()) return std::nullopt;
    if (ref.create && (priv || parsed != ResType::Id)) return std::nullopt;
    ref.name = {std::string(package), parsed.value_or(ResType::Anim), std::string(entry),
                otherType};
    ref.name.privateReference = priv;
    return ref;
  }
  if (t[0] == '?') {
    extractResourceName(t.substr(1), &package, &type, &entry);
    if ((!type.empty() && type != "attr") || entry.empty()) return std::nullopt;
    ref.attr = true;
    ref.name = {std::string(package), ResType::Attr, std::string(entry)};
    return ref;
  }
  return std::nullopt;
}

std::optional<std::string> buildString(std::string_view raw, std::string* error) {
  StringBuilder builder;
  builder.appendText(raw);
  if (!builder.error().empty()) {
    *error = builder.error();
    return std::nullopt;
  }
  return std::move(builder.text());
}

}  // namespace hydroid::res::detail

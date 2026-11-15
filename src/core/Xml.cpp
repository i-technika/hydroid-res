#include "core/Xml.h"

#include <algorithm>
#include <cstring>

namespace hydroid::res::detail {
namespace {

// Text whitespace and all: aapt2 reads the spaces between two tags of a string too.
constexpr unsigned kParseOptions = pugi::parse_default | pugi::parse_ws_pcdata;

// The URI that xmlns:`prefix` (xmlns="" for an empty one) declares on `node` or an ancestor.
std::optional<std::string> namespaceOf(pugi::xml_node node, std::string_view prefix) {
  if (prefix == "xml") return std::string("http://www.w3.org/XML/1998/namespace");
  const std::string attr = prefix.empty() ? "xmlns" : "xmlns:" + std::string(prefix);
  for (; node; node = node.parent()) {
    if (pugi::xml_attribute a = node.attribute(attr.c_str())) return std::string(a.value());
  }
  return std::nullopt;
}

// Whether the prefix of `name`, if it has one, is declared where `node` is.
bool bound(pugi::xml_node node, std::string_view name, bool attribute) {
  const size_t colon = name.find(':');
  if (colon == std::string_view::npos) return true;
  const std::string_view prefix = name.substr(0, colon);
  return (attribute && prefix == "xmlns") || namespaceOf(node, prefix).has_value();
}

// The first element that has, or whose attribute has, a prefix no xmlns declares.
pugi::xml_node unboundPrefix(pugi::xml_node element) {
  if (!bound(element, element.name(), false)) return element;
  for (pugi::xml_attribute a : element.attributes()) {
    if (!bound(element, a.name(), true)) return element;
  }
  for (pugi::xml_node child : element.children()) {
    if (child.type() != pugi::node_element) continue;
    if (pugi::xml_node found = unboundPrefix(child)) return found;
  }
  return {};
}

}  // namespace

bool XmlDocument::load(std::string_view text, std::string* error, int* errorLine) {
  lineStarts_ = {0};
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\n') lineStarts_.push_back(i + 1);
  }
  const pugi::xml_parse_result result = doc_.load_buffer(text.data(), text.size(), kParseOptions);
  if (!result) {
    *error = result.description();
    *errorLine = lineAt(result.offset);
    return false;
  }
  if (!doc_.document_element()) {
    *error = "no element found";
    *errorLine = static_cast<int>(lineStarts_.size());
    return false;
  }
  // expat reads namespaces, and fails on a prefix no xmlns declares.
  if (pugi::xml_node node = unboundPrefix(doc_.document_element())) {
    *error = "unbound prefix";
    *errorLine = line(node);
    return false;
  }
  return true;
}

int XmlDocument::line(pugi::xml_node node) const { return lineAt(node.offset_debug()); }

int XmlDocument::lineAt(ptrdiff_t offset) const {
  if (offset < 0) return 0;
  const auto it =
      std::upper_bound(lineStarts_.begin(), lineStarts_.end(), static_cast<size_t>(offset));
  return static_cast<int>(it - lineStarts_.begin());
}

std::string_view localName(const char* name) {
  const char* colon = std::strchr(name, ':');
  return colon ? std::string_view(colon + 1) : std::string_view(name);
}

std::optional<std::string> elementNamespace(pugi::xml_node element) {
  const std::string_view name = element.name();
  const size_t colon = name.find(':');
  if (colon == std::string_view::npos) return namespaceOf(element, "").value_or("");
  return namespaceOf(element, name.substr(0, colon));
}

bool isNamespaceDeclaration(pugi::xml_attribute attr) {
  const std::string_view name = attr.name();
  return name == "xmlns" || name.rfind("xmlns:", 0) == 0;
}

std::string resolvePackage(pugi::xml_node node, const std::string& alias) {
  if (alias.empty()) return alias;
  const std::optional<std::string> uri = namespaceOf(node, alias);
  if (!uri) return alias;
  constexpr std::string_view kPublic = "http://schemas.android.com/apk/res/";
  constexpr std::string_view kPrivate = "http://schemas.android.com/apk/prv/res/";
  if (*uri == "http://schemas.android.com/apk/res-auto") return "";
  for (std::string_view prefix : {kPublic, kPrivate}) {
    if (uri->size() > prefix.size() && uri->compare(0, prefix.size(), prefix) == 0) {
      return uri->substr(prefix.size());
    }
  }
  return alias;
}

}  // namespace hydroid::res::detail

#include "core/Xml.h"

#include <expat.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace hydroid::res::detail {
namespace {

// The URI that xmlns:`prefix` (xmlns="" for an empty one) declares on `node` or an ancestor.
std::optional<std::string> namespaceOf(pugi::xml_node node, std::string_view prefix) {
  if (prefix == "xml") return std::string("http://www.w3.org/XML/1998/namespace");
  const std::string attr = prefix.empty() ? "xmlns" : "xmlns:" + std::string(prefix);
  for (; node; node = node.parent()) {
    if (pugi::xml_attribute a = node.attribute(attr.c_str())) return std::string(a.value());
  }
  return std::nullopt;
}

// The separator aapt2's kXmlNamespaceSep gives expat, which fails on a namespace URI that has it.
constexpr XML_Char kNamespaceSeparator = 1;

// "p:name" of the "uri<sep>name<sep>p" expat gives for a name with a prefix, and "name" of
// "uri<sep>name" (a default namespace) or "name" (none).
std::string qualifiedName(const XML_Char* expanded) {
  const std::string_view name = expanded;
  const size_t uriEnd = name.find(kNamespaceSeparator);
  if (uriEnd == std::string_view::npos) return std::string(name);
  const size_t localEnd = name.find(kNamespaceSeparator, uriEnd + 1);
  const std::string_view local = name.substr(uriEnd + 1, localEnd - (uriEnd + 1));
  if (localEnd == std::string_view::npos) return std::string(local);
  return std::string(name.substr(localEnd + 1)) + ":" + std::string(local);
}

// The tree of what expat reads, as aapt2 gets it: the entities a DTD declares expanded, an
// undeclared one skipped past an external DTD, and the attributes an ATTLIST defaults added after
// those written. Elements keep their prefixed names and xmlns attributes. Text is a node for each
// piece expat reports, with the line expat gives it, as XmlPullParser makes a text event of each:
// a value joins them, and each piece of plain text between resources is an error of its own.
struct TreeBuilder {
  XML_Parser parser;
  XmlExtent extent;
  std::unordered_map<const void*, int>* lines;
  // The element being read, or the document before and after the root.
  pugi::xml_node node;
  // xmlns attributes for the next element, which expat reports before it.
  std::vector<std::pair<std::string, std::string>> namespaces{};
  bool inCdata = false;

  void add(pugi::xml_node added) {
    (*lines)[added.internal_object()] = static_cast<int>(XML_GetCurrentLineNumber(parser));
  }
};

TreeBuilder& builder(void* data) { return *static_cast<TreeBuilder*>(data); }

void XMLCALL startNamespace(void* data, const XML_Char* prefix, const XML_Char* uri) {
  builder(data).namespaces.emplace_back(prefix ? "xmlns:" + std::string(prefix) : "xmlns",
                                        uri ? uri : "");
}

void XMLCALL startElement(void* data, const XML_Char* name, const XML_Char** attrs) {
  TreeBuilder& b = builder(data);
  pugi::xml_node element = b.node.append_child(pugi::node_element);
  element.set_name(qualifiedName(name).c_str());
  for (const auto& [attr, uri] : b.namespaces) {
    element.append_attribute(attr.c_str()).set_value(uri.c_str());
  }
  b.namespaces.clear();
  for (; *attrs; attrs += 2) {
    element.append_attribute(qualifiedName(attrs[0]).c_str()).set_value(attrs[1]);
  }
  b.add(element);
  b.node = element;
}

void XMLCALL endElement(void* data, const XML_Char*) {
  TreeBuilder& b = builder(data);
  b.node = b.node.parent();
  // XmlExtent::RootElement: past the root's end tag, expat reads no more.
  if (b.extent == XmlExtent::RootElement && b.node.type() == pugi::node_document) {
    XML_StopParser(b.parser, XML_FALSE);
  }
}

void XMLCALL characters(void* data, const XML_Char* s, int length) {
  TreeBuilder& b = builder(data);
  pugi::xml_node text = b.node.append_child(b.inCdata ? pugi::node_cdata : pugi::node_pcdata);
  text.set_value(s, static_cast<size_t>(length));
  b.add(text);
}

void XMLCALL startCdata(void* data) { builder(data).inCdata = true; }

void XMLCALL endCdata(void* data) { builder(data).inCdata = false; }

}  // namespace

bool XmlDocument::load(std::string_view text, XmlExtent extent, std::string* error,
                       int* errorLine) {
  doc_.reset();
  lines_.clear();
  const std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> parser(
      XML_ParserCreateNS(nullptr, kNamespaceSeparator), XML_ParserFree);
  if (!parser) {
    *error = "out of memory";
    *errorLine = 0;
    return false;
  }
  TreeBuilder builder{parser.get(), extent, &lines_, doc_};
  XML_SetUserData(parser.get(), &builder);
  XML_SetReturnNSTriplet(parser.get(), XML_TRUE);
  XML_SetStartNamespaceDeclHandler(parser.get(), startNamespace);
  XML_SetElementHandler(parser.get(), startElement, endElement);
  XML_SetCharacterDataHandler(parser.get(), characters);
  XML_SetCdataSectionHandler(parser.get(), startCdata, endCdata);
  // XML_Parse takes an int length, so a text past INT_MAX goes in parts.
  for (std::string_view rest = text;;) {
    const size_t size = std::min<size_t>(rest.size(), INT_MAX);
    const bool last = size == rest.size();
    if (XML_Parse(parser.get(), rest.data(), static_cast<int>(size), last) != XML_STATUS_OK) {
      // XML_StopParser at the root's end tag.
      if (XML_GetErrorCode(parser.get()) == XML_ERROR_ABORTED) return true;
      *error = XML_ErrorString(XML_GetErrorCode(parser.get()));
      *errorLine = static_cast<int>(XML_GetCurrentLineNumber(parser.get()));
      return false;
    }
    if (last) return true;
    rest.remove_prefix(size);
  }
}

int XmlDocument::line(pugi::xml_node node) const {
  const auto it = lines_.find(node.internal_object());
  return it == lines_.end() ? 0 : it->second;
}

std::string_view localName(const char* name) {
  const char* colon = std::strchr(name, ':');
  return colon ? std::string_view(colon + 1) : std::string_view(name);
}

std::string elementNamespace(pugi::xml_node element) {
  const std::string_view name = element.name();
  const size_t colon = name.find(':');
  return namespaceOf(element, colon == std::string_view::npos ? "" : name.substr(0, colon))
      .value_or("");
}

std::string attributeNamespace(pugi::xml_node element, pugi::xml_attribute attr) {
  const std::string_view name = attr.name();
  const size_t colon = name.find(':');
  if (colon == std::string_view::npos) return {};
  return namespaceOf(element, name.substr(0, colon)).value_or("");
}

bool isNamespaceDeclaration(pugi::xml_attribute attr) {
  const std::string_view name = attr.name();
  return name == "xmlns" || name.rfind("xmlns:", 0) == 0;
}

std::optional<std::string> namespacePackage(std::string_view uri) {
  constexpr std::string_view kPublic = "http://schemas.android.com/apk/res/";
  constexpr std::string_view kPrivate = "http://schemas.android.com/apk/prv/res/";
  if (uri == "http://schemas.android.com/apk/res-auto") return std::string();
  for (std::string_view prefix : {kPublic, kPrivate}) {
    if (uri.size() > prefix.size() && uri.compare(0, prefix.size(), prefix) == 0) {
      return std::string(uri.substr(prefix.size()));
    }
  }
  return std::nullopt;
}

std::string resolvePackage(pugi::xml_node node, const std::string& alias) {
  if (alias.empty()) return alias;
  const std::optional<std::string> uri = namespaceOf(node, alias);
  if (!uri) return alias;
  return namespacePackage(*uri).value_or(alias);
}

}  // namespace hydroid::res::detail

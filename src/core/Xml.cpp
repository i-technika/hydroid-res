#include "core/Xml.h"

#include <expat.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
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
// those written. Elements keep their prefixed names and xmlns attributes; text is one node from
// one tag, comment, processing instruction or CDATA section to the next, as pugixml makes it.
struct TreeBuilder {
  XML_Parser parser;
  XmlExtent extent;
  std::unordered_map<const void*, int>* lines;
  // The element being read, or the document before and after the root.
  pugi::xml_node node;
  // xmlns attributes for the next element, which expat reports before it.
  std::vector<std::pair<std::string, std::string>> namespaces{};
  // Text read since the last node, and the line where it starts, if there is any.
  std::string text{};
  std::optional<int> textLine{};
  bool inCdata = false;

  int currentLine() const { return static_cast<int>(XML_GetCurrentLineNumber(parser)); }
  void add(pugi::xml_node added, int line) { (*lines)[added.internal_object()] = line; }

  // Ends the text read so far as a node.
  void endText() {
    if (!textLine) return;
    pugi::xml_node added = node.append_child(inCdata ? pugi::node_cdata : pugi::node_pcdata);
    added.set_value(text.c_str());
    add(added, *textLine);
    text.clear();
    textLine.reset();
  }
};

TreeBuilder& builder(void* data) { return *static_cast<TreeBuilder*>(data); }

void XMLCALL startNamespace(void* data, const XML_Char* prefix, const XML_Char* uri) {
  builder(data).namespaces.emplace_back(prefix ? "xmlns:" + std::string(prefix) : "xmlns",
                                        uri ? uri : "");
}

void XMLCALL startElement(void* data, const XML_Char* name, const XML_Char** attrs) {
  TreeBuilder& b = builder(data);
  b.endText();
  pugi::xml_node element = b.node.append_child(pugi::node_element);
  element.set_name(qualifiedName(name).c_str());
  for (const auto& [attr, uri] : b.namespaces) {
    element.append_attribute(attr.c_str()).set_value(uri.c_str());
  }
  b.namespaces.clear();
  for (; *attrs; attrs += 2) {
    element.append_attribute(qualifiedName(attrs[0]).c_str()).set_value(attrs[1]);
  }
  b.add(element, b.currentLine());
  b.node = element;
}

void XMLCALL endElement(void* data, const XML_Char*) {
  TreeBuilder& b = builder(data);
  b.endText();
  b.node = b.node.parent();
  // XmlExtent::RootElement: past the root's end tag, expat reads no more.
  if (b.extent == XmlExtent::RootElement && b.node.type() == pugi::node_document) {
    XML_StopParser(b.parser, XML_FALSE);
  }
}

void XMLCALL characters(void* data, const XML_Char* s, int length) {
  TreeBuilder& b = builder(data);
  if (!b.textLine) b.textLine = b.currentLine();
  b.text.append(s, static_cast<size_t>(length));
}

void XMLCALL startCdata(void* data) {
  TreeBuilder& b = builder(data);
  b.endText();
  b.inCdata = true;
  // <![CDATA[]]> too is a node.
  b.textLine = b.currentLine();
}

void XMLCALL endCdata(void* data) {
  TreeBuilder& b = builder(data);
  b.endText();
  b.inCdata = false;
}

void XMLCALL comment(void* data, const XML_Char*) { builder(data).endText(); }

void XMLCALL instruction(void* data, const XML_Char*, const XML_Char*) { builder(data).endText(); }

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
  XML_SetCommentHandler(parser.get(), comment);
  XML_SetProcessingInstructionHandler(parser.get(), instruction);
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

#include "core/Xml.h"

#include <expat.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>

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

// The separator aapt2's kXmlNamespaceSep gives expat, which fails on a namespace URI that has it.
constexpr XML_Char kNamespaceSeparator = 1;

// expat reading up to the end of the root element (XmlExtent::RootElement), where it stops.
struct RootReader {
  XML_Parser parser;
  int depth = 0;
  // Where the root element ends, once it has.
  size_t end = 0;
};

void XMLCALL startElement(void* data, const XML_Char*, const XML_Char**) {
  static_cast<RootReader*>(data)->depth++;
}

void XMLCALL endElement(void* data, const XML_Char*) {
  RootReader& reader = *static_cast<RootReader*>(data);
  if (--reader.depth > 0) return;
  reader.end = static_cast<size_t>(XML_GetCurrentByteIndex(reader.parser) +
                                   XML_GetCurrentByteCount(reader.parser));
  XML_StopParser(reader.parser, XML_FALSE);
}

// How much of `text` expat, as aapt2 creates it, reads as `extent` says, if it finds no error
// there: else what it fails on and the line where. pugixml takes much that is not well-formed XML
// (a bare &, an unknown entity, an attribute twice, a prefix no xmlns declares), which aapt2
// rejects.
std::optional<size_t> wellFormed(std::string_view text, XmlExtent extent, std::string* error,
                                 int* errorLine) {
  const std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> parser(
      XML_ParserCreateNS(nullptr, kNamespaceSeparator), XML_ParserFree);
  if (!parser) {
    *error = "out of memory";
    *errorLine = 0;
    return std::nullopt;
  }
  RootReader reader{parser.get()};
  if (extent == XmlExtent::RootElement) {
    XML_SetUserData(parser.get(), &reader);
    XML_SetElementHandler(parser.get(), startElement, endElement);
  }
  // XML_Parse takes an int length, so a text past INT_MAX goes in parts.
  for (std::string_view rest = text;;) {
    const size_t size = std::min<size_t>(rest.size(), INT_MAX);
    const bool last = size == rest.size();
    if (XML_Parse(parser.get(), rest.data(), static_cast<int>(size), last) != XML_STATUS_OK) {
      // XML_StopParser at the root's end tag.
      if (XML_GetErrorCode(parser.get()) == XML_ERROR_ABORTED) return reader.end;
      *error = XML_ErrorString(XML_GetErrorCode(parser.get()));
      *errorLine = static_cast<int>(XML_GetCurrentLineNumber(parser.get()));
      return std::nullopt;
    }
    if (last) return text.size();
    rest.remove_prefix(size);
  }
}

}  // namespace

bool XmlDocument::load(std::string_view text, XmlExtent extent, std::string* error,
                       int* errorLine) {
  lineStarts_ = {0};
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\n') lineStarts_.push_back(i + 1);
  }
  const std::optional<size_t> read = wellFormed(text, extent, error, errorLine);
  if (!read) return false;
  const pugi::xml_parse_result result = doc_.load_buffer(text.data(), *read, kParseOptions);
  if (!result) {
    *error = result.description();
    *errorLine = lineAt(result.offset);
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

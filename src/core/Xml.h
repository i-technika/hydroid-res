// pugixml as aapt2's XML parser (expat, with namespaces) reads a file: element names and
// namespaces, the text whitespace and all, and the line of each node, for errors.

#pragma once

#include <pugixml.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydroid::res::detail {

inline constexpr std::string_view kXliffNamespace = "urn:oasis:names:tc:xliff:document:1.2";
inline constexpr std::string_view kAaptNamespace = "http://schemas.android.com/aapt";

class XmlDocument {
 public:
  // false if `text` is not well-formed XML or has no element, with what and the line where.
  bool load(std::string_view text, std::string* error, int* errorLine);
  pugi::xml_node root() const { return doc_.document_element(); }
  // The line of `node`, from 1.
  int line(pugi::xml_node node) const;

 private:
  int lineAt(ptrdiff_t offset) const;

  pugi::xml_document doc_;
  std::vector<size_t> lineStarts_;
};

// "g" of "xliff:g", and the name itself when it has no prefix.
std::string_view localName(const char* name);

// The namespace of `element`, from the xmlns of its prefix (or xmlns="" without one) on it or
// an ancestor: "" for none, nullopt for a prefix that no xmlns declares, which expat rejects.
std::optional<std::string> elementNamespace(pugi::xml_node element);

// Whether `attr` declares a namespace (xmlns="", xmlns:p=""), which expat does not report as an
// attribute.
bool isNamespaceDeclaration(pugi::xml_attribute attr);

// xml::ResolvePackage: the package that `alias` (the "app" of @app:color/x or app:attr) stands
// for where `node` is. A prefix declared as http://schemas.android.com/apk/res/<package> is that
// package, one declared as .../apk/res-auto the package being loaded (""), and anything else is
// left as it is.
std::string resolvePackage(pugi::xml_node node, const std::string& alias);

}  // namespace hydroid::res::detail

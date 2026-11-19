// A file as aapt2's XML parser (expat, with namespaces) reads it: expat itself decides whether it
// is well-formed and reads it, and its elements, attributes and text, the whitespace and all,
// are kept in a pugixml tree with the line of each node, for errors. pugixml reads no file
// itself, as it does not read a DTD's entities and default attributes as expat does (#104).

#pragma once

#include <pugixml.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace hydroid::res::detail {

inline constexpr std::string_view kXliffNamespace = "urn:oasis:names:tc:xliff:document:1.2";
inline constexpr std::string_view kAaptNamespace = "http://schemas.android.com/aapt";

// How much of a file aapt2 reads: all of it (xml::Inflate, an XML file), or up to the end of its
// root element (XmlPullParser, a values file), past which it looks for no error.
enum class XmlExtent { Document, RootElement };

class XmlDocument {
 public:
  // false if `text`, as far as `extent` reads it, is not well-formed XML or has no element, with
  // what and the line where.
  bool load(std::string_view text, XmlExtent extent, std::string* error, int* errorLine);
  pugi::xml_node root() const { return doc_.document_element(); }
  // The line of `node`, from 1.
  int line(pugi::xml_node node) const;

 private:
  pugi::xml_document doc_;
  // The line of each node, by its pugi::xml_node::internal_object().
  std::unordered_map<const void*, int> lines_;
};

// "g" of "xliff:g", and the name itself when it has no prefix.
std::string_view localName(const char* name);

// The namespace of `element`, from the xmlns of its prefix (or xmlns="" without one) on it or
// an ancestor, or the XML namespace for xml:, which needs none: "" for none. Every prefix has
// one, as load() fails on any other prefix that no xmlns declares, as expat does (#78), so a
// caller looks for no other case (#91).
std::string elementNamespace(pugi::xml_node element);

// Whether `attr` declares a namespace (xmlns="", xmlns:p=""), which expat does not report as an
// attribute.
bool isNamespaceDeclaration(pugi::xml_attribute attr);

// xml::ResolvePackage: the package that `alias` (the "app" of @app:color/x or app:attr) stands
// for where `node` is. A prefix declared as http://schemas.android.com/apk/res/<package> is that
// package, one declared as .../apk/res-auto the package being loaded (""), and anything else is
// left as it is.
std::string resolvePackage(pugi::xml_node node, const std::string& alias);

}  // namespace hydroid::res::detail

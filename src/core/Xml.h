// A file as aapt2's XML parser (expat, with namespaces) reads it: expat itself decides whether it
// is well-formed and reads it, and its elements, attributes and text, the whitespace and all,
// are kept in a pugixml tree with the line of each node, for errors. pugixml reads no file
// itself, as it does not read a DTD's entities and default attributes as expat does (#104).

#pragma once

#include <pugixml.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/Parsed.h"

namespace hydroid::res::detail {

inline constexpr std::string_view kXliffNamespace = "urn:oasis:names:tc:xliff:document:1.2";
inline constexpr std::string_view kAaptNamespace = "http://schemas.android.com/aapt";
inline constexpr std::string_view kToolsNamespace = "http://schemas.android.com/tools";

// How much of a file aapt2 reads: all of it (xml::Inflate, an XML file), or up to the end of its
// root element (XmlPullParser, a values file), past which it looks for no error.
enum class XmlExtent { Document, RootElement };

class XmlDocument {
 public:
  // false if `text`, as far as `extent` reads it, is not well-formed XML or has no element, with
  // what and the line where. Memory that runs out in the handlers of expat, as they allocate with
  // operator new, throws std::bad_alloc from here, and not through expat, which is C, however expat
  // was built (#256); in expat's own allocations it is "out of memory", and where pugixml has none,
  // which it says by an empty node or false, this throws std::bad_alloc too (#263).
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

// The namespace of the attribute `attr` of `element`, from the xmlns of its prefix: "" for one
// with no prefix, which a default xmlns="" does not give a namespace.
std::string attributeNamespace(pugi::xml_node element, pugi::xml_attribute attr);

// Whether `attr` declares a namespace (xmlns="", xmlns:p=""), which expat does not report as an
// attribute.
bool isNamespaceDeclaration(pugi::xml_attribute attr);

// xml::ExtractPackageFromNamespace: the package of http://schemas.android.com/apk/res/<package>
// or .../apk/prv/res/<package>, the package being loaded ("") of .../apk/res-auto, and nullopt of
// any other namespace.
struct NamespacePackage {
  std::string package;
  bool isPrivate = false;  // .../apk/prv/res/<package>, which makes a private reference (#141)
};
std::optional<NamespacePackage> namespacePackage(std::string_view uri);

// xml::ResolvePackage: the package of `name` as written (the "app" of @app:color/x or app:attr)
// becomes the one it stands for where `node` is: that of the nearest xmlns of its prefix, on
// `node` or an ancestor, that names a package (namespacePackage), past any that names another
// namespace, and anything else is left as it is. A namespace .../apk/prv/res/ makes `name` a
// private reference; a public one leaves @* private.
void resolvePackage(pugi::xml_node node, ResName& name);

}  // namespace hydroid::res::detail

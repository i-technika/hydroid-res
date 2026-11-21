// parseLocaleConfig: android.app.LocaleConfig.parseLocaleConfig on the file itself, as res/
// holds it, and not as aapt2 compiles it.

#include <set>
#include <string>
#include <string_view>

#include "core/ResScanner.h"
#include "core/Xml.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/Resources.h"

namespace hydroid::res {
namespace {

constexpr std::string_view kAndroidNamespace = "http://schemas.android.com/apk/res/android";

// The value of the attribute android:`name` of `element`, if it has one.
const char* androidAttribute(pugi::xml_node element, std::string_view name) {
  for (pugi::xml_attribute a : element.attributes()) {
    if (detail::isNamespaceDeclaration(a)) continue;
    if (detail::localName(a.name()) == name &&
        detail::attributeNamespace(element, a) == kAndroidNamespace) {
      return a.value();
    }
  }
  return nullptr;
}

}  // namespace

std::optional<LocaleConfig> parseLocaleConfig(const std::filesystem::path& file,
                                              std::vector<Diagnostic>* errors) {
  const auto fail = [&](int line, std::string message) -> std::optional<LocaleConfig> {
    if (errors != nullptr) errors->push_back({file, line, std::move(message)});
    return std::nullopt;
  };
  std::string text;
  if (const char* why = detail::readFile(file, &text)) return fail(0, why);
  detail::XmlDocument doc;
  std::string why;
  int line = 0;
  if (!doc.load(text, detail::XmlExtent::Document, &why, &line)) {
    return fail(line, "xml parser error: " + why);
  }
  // XmlUtils.beginDocument: the root must be <locale-config>.
  const pugi::xml_node root = doc.root();
  if (detail::localName(root.name()) != "locale-config") {
    return fail(doc.line(root), "expected <locale-config>, not <" + std::string(root.name()) + ">");
  }

  LocaleConfig config;
  // The names as written, of which the default must be one.
  std::set<std::string> names;
  // Only the children of the root, as XmlUtils.nextElementWithin gives them; others are skipped.
  for (pugi::xml_node child : root.children()) {
    if (child.type() != pugi::node_element || detail::localName(child.name()) != "locale") {
      continue;
    }
    const char* name = androidAttribute(child, "name");
    if (name == nullptr) return fail(doc.line(child), "<locale> has no android:name");
    if (!names.insert(name).second) continue;
    std::optional<Config> locale = parseLanguageTag(name);
    if (!locale) return fail(doc.line(child), "'" + std::string(name) + "' is no language tag");
    config.supported.push_back(*locale);
  }

  if (const char* defaultLocale = androidAttribute(root, "defaultLocale")) {
    // LocaleConfig takes it only if it is, as written, one of the names.
    if (names.count(defaultLocale) == 0) {
      return fail(doc.line(root), "default locale '" + std::string(defaultLocale) +
                                      "' is not one of the <locale> names");
    }
    config.defaultLocale = parseLanguageTag(defaultLocale);
  }
  return config;
}

}  // namespace hydroid::res

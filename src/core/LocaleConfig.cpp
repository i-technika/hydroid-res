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

// Locale.forLanguageTag, which leaves out the first subtag that is not well-formed and all those
// after it: the locale of the longest run of subtags from the first that parseLanguageTag reads,
// or nullopt if there is none, for the root locale, which has no language. Where parseLanguageTag
// and the JDK differ, it is as parseLanguageTag reads: an extended language, as yue of
// zh-yue-HK, which the JDK takes for the language, is read as no well-formed subtag, so zh.
std::optional<Config> forLanguageTag(std::string_view tag) {
  for (;;) {
    if (std::optional<Config> locale = parseLanguageTag(tag)) return locale;
    const size_t dash = tag.rfind('-');
    if (dash == std::string_view::npos) return std::nullopt;
    tag = tag.substr(0, dash);
  }
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
    // LocaleConfig joins the names with ',' and has LocaleList.forLanguageTags split them again,
    // so a name of several is several locales. A <locale> without a name is "null" to it, a
    // language no user's locale is of, and a name with no language is the root locale, which
    // matches no other: both are left out, as they would change nothing.
    const char* name = androidAttribute(child, "name");
    if (name == nullptr || !names.insert(name).second) continue;
    std::string_view rest = name;
    for (;;) {
      const size_t comma = rest.find(',');
      if (std::optional<Config> locale = forLanguageTag(rest.substr(0, comma))) {
        // LocaleList drops a locale it already has.
        bool seen = false;
        for (const Config& s : config.supported) seen = seen || s == *locale;
        if (!seen) config.supported.push_back(*locale);
      }
      if (comma == std::string_view::npos) break;
      rest.remove_prefix(comma + 1);
    }
  }

  if (const char* defaultLocale = androidAttribute(root, "defaultLocale")) {
    // LocaleConfig takes it only if it is, as written, one of the names.
    if (names.count(defaultLocale) == 0) {
      return fail(doc.line(root), "default locale '" + std::string(defaultLocale) +
                                      "' is not one of the <locale> names");
    }
    // A default with no language is the root locale, which ResourcesImpl gives AssetManager2 as
    // "und": a language of its own, which no value is of.
    config.defaultLocale = forLanguageTag(defaultLocale);
    if (!config.defaultLocale) config.defaultLocale = parseLanguageTag("und");
  }
  return config;
}

}  // namespace hydroid::res

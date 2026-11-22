#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/Export.h"
#include "hydroid/res/ResId.h"
#include "hydroid/res/ResourceTable.h"

// The values of a table for one device: of the values a resource has, each for a configuration,
// the one Android picks, as its AssetManager2::FindEntry does with ResTable_config::match and
// isBetterThan (Config.h). golden/data holds what Android itself picked of the golden app's res/
// under some hundreds of device configurations, and GoldenMatchTest checks these against it.

namespace hydroid::res {

// The value of `entry` for `device`, or nullptr if none matches it: of the candidates that match,
// the first, in their order (ResourceTable.h), then each one that isBetterThan the one held. That
// is the order Android meets them in, in the table aapt2 writes, and it matters, as isBetterThan
// is not an order.
HYDROID_RES_API const Candidate* bestCandidate(const Entry& entry, const Config& device);

// The locales an app says it has (android.app.LocaleConfig): those of the file its manifest names
// by android:localeConfig, and, from Android 15, the one of them its values without a language
// are in, android:defaultLocale. Each is a locale alone, as parseLanguageTag (ConfigParser.h)
// makes it. Resources::setLocaleConfig says what the default changes.
struct LocaleConfig {
  std::vector<Config> supported;
  std::optional<Config> defaultLocale;  // one of `supported`, if any
};

// LocaleConfig.parseLocaleConfig: the LocaleConfig of a file such as res/xml/locales_config.xml:
// a root <locale-config>, with android:defaultLocale perhaps, holding a <locale> for each locale,
// which android:name gives. Anything else in the root is passed over. A name is read as
// LocaleList.forLanguageTags reads it: each of its tags between commas up to its first subtag
// that is not well-formed ("en-US-ab" is en-US), as parseLanguageTag reads it, which takes no
// extended language (zh-yue-HK is zh, where Java has yue-HK). A <locale> without a name, a
// language "null" to Android, or of a name with no language, the root locale, matches none of
// the user's, and is left out. A default of no language is "und", as ResourcesImpl gives it.
// nullopt, with what and where in `errors` if given, if the file cannot be read, is no XML, has
// another root, or has a default that is not, as written, one of the names: Android has no default
// for an app then, and the locales without one change nothing.
HYDROID_RES_API std::optional<LocaleConfig> parseLocaleConfig(
    const std::filesystem::path& file, std::vector<Diagnostic>* errors = nullptr);

// A table and the configuration of the device it is read for.
//
// Every member may be called from any thread at any time, but not while the table changes. A
// candidate it returns is of the table: it stays what it is as the configuration changes, and
// goes as the table's own entries do, when the table changes: when it adds a package, or is
// assigned or moved from (its revision, ResourceTable.h).
class Resources {
 public:
  // For `table`, which must outlive this, and the default configuration, which takes the values
  // without qualifiers, or with a version up to kEmulatedSdkVersion (Version.h), and those of a
  // density, which never keeps a value from matching. It asks for no density, which is mdpi, as
  // is a value of none: values-anydpi/ comes before all, then values-mdpi/, as good as values/
  // and after it in the table, then values/, then the other densities.
  HYDROID_RES_API explicit Resources(const ResourceTable& table);
  HYDROID_RES_API ~Resources();
  HYDROID_RES_API Resources(Resources&& other) noexcept;
  HYDROID_RES_API Resources& operator=(Resources&& other) noexcept;

  // Sets the state of the device, as Android's ResourcesImpl::updateConfiguration takes it:
  //   - `locales`, if there are any, are the user's languages, the most wanted first, of which
  //     only the locale is read (parseLanguageTag, ConfigParser.h, makes one), and the locale of
  //     `device` is replaced by one of them: the first that the table has resources for, or that
  //     is an English, which Android takes every app to have without a language; or the very
  //     first if there is none, or only one locale. The table has resources for a locale when
  //     one of its values, outside the package android (kFrameworkPackageId), is of its
  //     language and script, the script being worked out as in matches (Config.h); of all the
  //     packages, if those have values of no language. Without any, the first locale it is. The
  //     choice is for the whole table and not for each resource: a resource that has no value
  //     for the chosen language takes its value without a language, not that of the user's next.
  //     That is unless the app has a default locale (setLocaleConfig), when more than one locale
  //     may be read in, as ResourcesImpl.updateConfiguration has it. It keeps the locales of its
  //     configuration as each call leaves them, and compares `locales` with those:
  //       - Others, of more than one: with a default, those that one the app supports matches by
  //         language and script are read in, in their order, and become the configuration's,
  //         if there are any. If not, or without a default, one is chosen for the whole table as
  //         above, and the configuration's are `locales` with that one moved to the front. If
  //         the table has no locales but pseudo-locales, they are `locales` as they are.
  //       - The same, or one: with a default, all of the configuration's are read in, whether
  //         the app supports them or not; without, the first.
  //     So a Resources made for fr and es reads in fr alone, and, given a default, at the next
  //     call of the same locales, fr and es; one made for de, fr and es, de being one the table
  //     has, reads in de alone, and then de, fr and es. Android gives an app's LocaleConfig to the
  //     Resources it makes after they have chosen, and only if they have more than one locale then
  //     (ContextImpl.setResources), which a caller does by calling setConfiguration, then
  //     setLocaleConfig. Each resource takes, of the values each of those read in picks, the first
  //     that is of a locale, a value without one being of the default where that matches the locale
  //     it was picked for; so a resource without a value of the first language takes that of the
  //     next. One whose values are for an mcc or mnc the device has comes before one that is not,
  //     whatever its locale (isBetterThanBeforeLocale). A resource that has no value for one of
  //     those locales has none at all, as on Android. configuration() has the first of them. A
  //     table that changes has the locales chosen as if `locales` were others.
  //     golden/localeconfig/data holds what Android picked through such calls, and
  //     GoldenLocaleConfigTest checks it.
  //   - A language that has an old code is given that code, which is the one Android's resources
  //     go by: he is iw, id is in, yi is ji. So values-iw/ is Hebrew, and values-he/ is not
  //     picked for it.
  //   - A version of 0, with no minor version, is kEmulatedSdkVersion. A minor version alone is
  //     kept as it is.
  //   - The longer side of the screen in pixels is its width, so a phone held upright, of
  //     1080x2400, is 2400x1080 and takes values-2000x1000/, not values-800x480/.
  //   - navHidden is Any, as Android tells its resources nothing of it.
  // The generation goes up if that changes what is picked (generation()). Each Resources looks for
  // the locales of the table in all its values once for each revision of it, and chooses the locale
  // again only when `locales`, the table or the LocaleConfig have changed, so a call that changes
  // the rest of the device, as a window is resized, costs little however many values the table
  // has.
  HYDROID_RES_API void setConfiguration(const Config& device,
                                        const std::vector<Config>& locales = {});

  // Sets the locales the app says it has, as ResourcesImpl.setLocaleConfig, which Android gives
  // the LocaleConfig of the app's manifest (parseLocaleConfig reads its file): its default
  // locale, if it has one, has the resources read in more than one locale, as setConfiguration
  // says. As on Android, it changes nothing until the next setConfiguration, which takes it in
  // whatever it is given: a table that changes before that has the locales chosen with the
  // LocaleConfig before. An empty LocaleConfig, which a Resources starts with, is an app without
  // one.
  HYDROID_RES_API void setLocaleConfig(const LocaleConfig& config);

  // The configuration the values are picked for: the device's as setConfiguration made it, with
  // the locale it chose, or the first of those it reads in.
  HYDROID_RES_API Config configuration() const;

  // A number that changes whenever what `candidate` returns may: when setConfiguration changes
  // the configurations the values are picked for, or, of more than one, the default locale they
  // are read with, and when the table has changed. What is derived from the values can be
  // kept by it: by the one `candidate` gives with them, or by one read before them, which at
  // worst is older than they are, and has them thrown away at the next look. Not by one read
  // after them: setConfiguration may have been called in between, on another thread, and the
  // values of the configuration before would be kept as those of the new one.
  HYDROID_RES_API uint32_t generation() const;

  // The value of the resource `id` for the configuration, or nullptr if the table has no such
  // resource, or none of its values matches. The answers are kept until the generation changes.
  // `generation`, if given, is set to the generation the answer is of.
  HYDROID_RES_API const Candidate* candidate(ResId id, uint32_t* generation = nullptr) const;

  // As `candidate`, for the configuration with another density, as Resources.getValueForDensity
  // and getDrawableForDensity have AssetManager2::FindEntry pick with its density_override:
  // `density` as Config::density has it, and 0 for the device's own. Nothing else of the
  // configuration changes, nor does the generation; the answers are kept as those of `candidate`
  // are, for each density.
  HYDROID_RES_API const Candidate* candidateForDensity(ResId id, uint16_t density,
                                                       uint32_t* generation = nullptr) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hydroid::res

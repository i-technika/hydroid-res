#include "hydroid/res/Resources.h"

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "core/LocaleData.h"
#include "core/LocalePack.h"
#include "hydroid/res/Version.h"

namespace hydroid::res {
namespace {

using Lang = std::array<char, 4>;

constexpr Lang kEn = {'e', 'n'};
constexpr Lang kAr = {'a', 'r'};
constexpr Lang kTl = {'t', 'l'};
constexpr Lang kFil = {'f', 'i', 'l'};

// The languages with two codes: the old one, which Android's resources go by, and the one
// java.util.Locale and BCP 47 give them now.
constexpr std::pair<Lang, Lang> kOldAndNewCodes[] = {
    {{'i', 'w'}, {'h', 'e'}},
    {{'i', 'n'}, {'i', 'd'}},
    {{'j', 'i'}, {'y', 'i'}},
};

// ResourcesImpl.adjustLanguageTag: the language under its old code.
Lang oldCode(const Lang& lang) {
  for (const auto& [oldLang, newLang] : kOldAndNewCodes) {
    if (lang == newLang) return oldLang;
  }
  return lang;
}

// The language as a java.util.Locale holds it, under its new code.
Lang newCode(const Lang& lang) {
  for (const auto& [oldLang, newLang] : kOldAndNewCodes) {
    if (lang == oldLang) return newLang;
  }
  return lang;
}

// The locale of a Config alone.
struct Locale {
  Lang lang{};
  std::array<char, 4> region{};
  std::array<char, 4> script{};
  std::array<char, 8> variant{};

  auto fields() const { return std::tie(lang, region, script, variant); }
  bool operator==(const Locale& o) const { return fields() == o.fields(); }
  bool operator<(const Locale& o) const { return fields() < o.fields(); }
};

// As a java.util.Locale has it, which is how LocaleList compares locales.
Locale javaLocaleOf(const Config& c) { return {newCode(c.lang), c.region, c.script, c.variant}; }

// LocaleList.isPseudoLocale: en-XA and ar-XB, the locales of made-up text to test layouts with.
bool isPseudoLocale(const Locale& l) {
  if (l.script[0] != '\0' || l.variant[0] != '\0') return false;
  return (l.lang == kEn && l.region == std::array<char, 4>{'X', 'A'}) ||
         (l.lang == kAr && l.region == std::array<char, 4>{'X', 'B'});
}

// LocaleList.getLikelyScript: the script the locale names, or the one its language is most likely
// written in. Android asks ICU for that; this asks the tables Android matches resources by, which
// are made from ICU's.
detail::ScriptCode likelyScript(const Locale& l) {
  if (l.script[0] != '\0') return l.script;
  return detail::computeScript(detail::packLanguage(l.lang), detail::packRegion(l.region));
}

// LocaleList.matchesLanguageAndScript.
bool matchesLanguageAndScript(const Locale& supported, const Locale& desired) {
  if (supported == desired) return true;
  if (supported.lang != desired.lang) return false;
  if (isPseudoLocale(supported) || isPseudoLocale(desired)) return false;
  const detail::ScriptCode supportedScript = likelyScript(supported);
  if (supportedScript[0] == '\0') {
    // Nothing is known of the language: the regions must be the same, as before Android 7, unless
    // the table's names none.
    return supported.region[0] == '\0' || supported.region == desired.region;
  }
  return supportedScript == likelyScript(desired);
}

constexpr size_t kNoMatch = static_cast<size_t>(-1);

// LocaleList.findFirstMatchIndex.
size_t findFirstMatchIndex(const std::vector<Locale>& desired, const Locale& supported) {
  for (size_t i = 0; i < desired.size(); i++) {
    if (matchesLanguageAndScript(supported, desired[i])) return i;
  }
  return kNoMatch;
}

// LocaleList.computeFirstMatchIndex, with English taken to be supported: of the locales the user
// wants, which are more than one, the first that one of `supported` matches.
size_t computeFirstMatchIndex(const std::vector<Locale>& desired,
                              const std::set<Locale>& supported) {
  size_t best = findFirstMatchIndex(desired, Locale{kEn, {}, {'L', 'a', 't', 'n'}, {}});
  if (best == 0) return 0;
  for (const Locale& locale : supported) {
    const size_t index = findFirstMatchIndex(desired, locale);
    if (index == 0) return 0;
    if (index < best) best = index;
  }
  return best == kNoMatch ? 0 : best;
}

// The locales the values of a table are for, as AssetManager2::GetResourceLocales gives them to
// AssetManager.getNonSystemLocales and getLocales: with Tagalog as Filipino
// (ResTable_config::getBcp47Locale, canonicalizing).
struct TableLocales {
  std::set<Locale> app;     // of the packages other than android (kFrameworkPackageId)
  std::set<Locale> system;  // of the package android
};

// Goes through every value of the table, so each Resources makes it once for each revision of it.
TableLocales localesOf(const ResourceTable& table) {
  TableLocales locales;
  for (const Package& package : table.packages()) {
    std::set<Locale>& into = package.id == kFrameworkPackageId ? locales.system : locales.app;
    for (const TypeTable& type : package.types) {
      for (const Entry& entry : type.entries) {
        for (const Candidate& candidate : entry.candidates) {
          const Config& c = candidate.config;
          if (c.lang[0] == '\0') continue;
          Locale locale = javaLocaleOf(c);
          if (locale.lang == kTl) locale.lang = kFil;
          into.insert(locale);
        }
      }
    }
  }
  return locales;
}

// LocaleList.isPseudoLocalesOnly.
bool isPseudoLocalesOnly(const std::set<Locale>& locales) {
  for (const Locale& locale : locales) {
    if (!isPseudoLocale(locale)) return false;
  }
  return true;
}

// ResourcesImpl.updateConfiguration: the index of the one of `locales`, which are more than one,
// that the resources of a table of `available` locales are read in, or kNoMatch if the table has
// no locales but pseudo-locales, of the app's or the system's.
size_t chooseLocaleIndex(const std::vector<Config>& locales, const TableLocales& available) {
  const std::set<Locale>* supported = &available.app;
  std::set<Locale> all;
  if (isPseudoLocalesOnly(available.app)) {
    // No locales of the app's own, so the system's.
    all = available.app;
    all.insert(available.system.begin(), available.system.end());
    if (isPseudoLocalesOnly(all)) return kNoMatch;
    supported = &all;
  }
  std::vector<Locale> desired;
  desired.reserve(locales.size());
  for (const Config& locale : locales) desired.push_back(javaLocaleOf(locale));
  return computeFirstMatchIndex(desired, *supported);
}

// LocaleList.getIntersection: the indexes of those of `locales` that one of `supported` matches
// by language and script, in their order.
std::vector<size_t> intersection(const std::vector<Config>& locales,
                                 const std::vector<Config>& supported) {
  std::vector<size_t> out;
  for (size_t i = 0; i < locales.size(); i++) {
    const Locale desired = javaLocaleOf(locales[i]);
    for (const Config& s : supported) {
      if (matchesLanguageAndScript(javaLocaleOf(s), desired)) {
        out.push_back(i);
        break;
      }
    }
  }
  return out;
}

// ResTable_config::isBetterThanBeforeLocale: whether `a` is better than `b` for `device` by what
// comes before the locale, the mcc and mnc.
bool isBetterThanBeforeLocale(const Config& a, const Config& b, const Config& device) {
  if (a.mcc != 0 || a.mnc != 0 || b.mcc != 0 || b.mnc != 0) {
    if (a.mcc != b.mcc && device.mcc != 0) return a.mcc != 0;
    if (a.mnc != b.mnc && device.mnc != 0) return a.mnc != 0;
  }
  return false;
}

// The configuration setConfiguration makes of a device's state and the locale chosen for it, if
// there is one.
Config effectiveConfig(const Config& device, const Config* locale) {
  Config config = device;
  if (locale != nullptr) {
    config.lang = locale->lang;
    config.region = locale->region;
    config.script = locale->script;
    config.variant = locale->variant;
  }
  config.lang = oldCode(config.lang);
  if (config.sdkVersion == 0 && config.minorVersion == 0) {
    config.sdkVersion = static_cast<uint16_t>(kEmulatedSdkVersion);
  }
  // ResourcesImpl.updateConfiguration gives the display its longer side as the width, however the
  // device is held, and AssetManager.setConfiguration takes no navigationHidden.
  if (config.screenWidth < config.screenHeight) std::swap(config.screenWidth, config.screenHeight);
  config.navHidden = NavHidden::Any;
  return config;
}

// `config` with the script of its locale, where it names none and the tables know one, which
// matches and isBetterThan otherwise work out for every value they are asked about.
Config withScript(const Config& config) {
  Config out = config;
  if (out.script[0] == 0) {
    out.script =
        detail::computeScript(detail::packLanguage(out.lang), detail::packRegion(out.region));
  }
  return out;
}

// The default locale of `config`, as ResourcesImpl gives it to AssetManager2: a locale alone,
// under its old code.
std::optional<Config> defaultLocaleOf(const LocaleConfig& config) {
  if (!config.defaultLocale) return std::nullopt;
  Config locale;
  locale.lang = oldCode(config.defaultLocale->lang);
  locale.region = config.defaultLocale->region;
  locale.script = config.defaultLocale->script;
  locale.variant = config.defaultLocale->variant;
  return locale;
}

}  // namespace

const Candidate* bestCandidate(const Entry& entry, const Config& device) {
  const Candidate* best = nullptr;
  for (const Candidate& candidate : entry.candidates) {
    if (!matches(candidate.config, device)) continue;
    if (best == nullptr || isBetterThan(candidate.config, best->config, device)) best = &candidate;
  }
  return best;
}

struct Resources::Impl {
  explicit Impl(const ResourceTable& t) : table(t), tableRevision(t.revision()) {
    prepare(nullptr, 0, device);
    configs.swap(spareConfigs);
    matchConfigs.swap(spareMatchConfigs);
  }

  // Makes into `spareConfigs` effectiveConfig of `d` and each of the `n` locales at `l`, or of `d`
  // alone, and into `spareMatchConfigs` withScript of each: what a call makes before it changes
  // anything, so that one that throws leaves all as it was. They hold the configurations before
  // the last that changed them, so one that changes only the device has the room it needs, and
  // allocates nothing.
  void prepare(const Config* l, size_t n, const Config& d) {
    spareConfigs.clear();
    spareMatchConfigs.clear();
    if (n == 0) {
      spareConfigs.push_back(effectiveConfig(d, nullptr));
    } else {
      for (size_t i = 0; i < n; i++) spareConfigs.push_back(effectiveConfig(d, &l[i]));
    }
    for (const Config& config : spareConfigs) spareMatchConfigs.push_back(withScript(config));
    // The room the configurations before need to become the spare ones, which reserve() makes
    // without changing what they hold, if it throws.
    configs.reserve(spareConfigs.size());
    matchConfigs.reserve(spareMatchConfigs.size());
  }

  // Makes those prepare() made what the values are picked for, with the default locale of
  // `localeConfig`. That changes what is picked if the configurations, or the default locale
  // they are read with, are not those already, or the table has changed since.
  void commit(const std::optional<Config>& newDefault, uint64_t revision) noexcept {
    const bool sameDefault = newDefault == defaultLocale || spareConfigs.size() == 1;
    defaultLocale = newDefault;
    if (spareConfigs == configs && sameDefault && revision == tableRevision) return;
    configs.swap(spareConfigs);
    matchConfigs.swap(spareMatchConfigs);
    tableRevision = revision;
    clearCaches();
    generation++;
  }

  // ResourcesImpl.updateConfiguration, for the locales `of` given and the LocaleConfig `app`, with
  // Android's configuration's locales `before` the update, if `changed` is not already known to
  // be so: the locales the configuration has after it, and those the resources are read in,
  // which leave nothing changed if it throws but the locales of the table it keeps.
  struct Choice {
    std::vector<Config> configLocales;
    std::vector<Config> readIn;
  };

  Choice choose(const std::vector<Config>& of, const std::vector<Config>& before, bool changed,
                uint64_t revision, const LocaleConfig& app) {
    Choice out;
    changed = changed || of != before;
    out.configLocales = changed ? of : before;
    bool chosen = false;
    if (changed && of.size() > 1) {
      // Of the locales the app supports, those of the list, which the configuration is made of.
      if (app.defaultLocale) {
        for (const size_t i : intersection(of, app.supported)) out.readIn.push_back(of[i]);
        if (!out.readIn.empty()) {
          out.configLocales = out.readIn;
          chosen = true;
        }
      }
      if (!chosen) {
        if (!tableLocales || tableLocalesRevision != revision) {
          tableLocales = localesOf(table);
          tableLocalesRevision = revision;
        }
        const size_t index = chooseLocaleIndex(of, *tableLocales);
        if (index != kNoMatch) {
          out.readIn = {of[index]};
          chosen = true;
          // The one chosen first in the configuration, if it is not already.
          if (index != 0) {
            out.configLocales.clear();
            out.configLocales.push_back(of[index]);
            for (size_t i = 0; i < of.size(); i++) {
              if (i != index) out.configLocales.push_back(of[i]);
            }
          }
        }
      }
    }
    // Locales that are as they were, one, or a table of none but pseudo-locales: all of the
    // configuration's with a default locale, or its first.
    if (!chosen && !out.configLocales.empty()) {
      if (app.defaultLocale) {
        out.readIn = out.configLocales;
      } else {
        out.readIn = {out.configLocales.front()};
      }
    }
    return out;
  }

  // The locales the resources are read in at an update that gives the same locales as the last,
  // where nothing else has changed but the device: what choose() would make of them, without
  // allocating. Where they are the configuration's, as they are left, all of these or the first;
  // otherwise, as it would choose the same again, those it chose.
  std::pair<const Config*, size_t> sameAgain() const {
    if (locales != configLocales || locales.empty()) return {readIn.data(), readIn.size()};
    if (chosenWith.defaultLocale) return {configLocales.data(), configLocales.size()};
    return {configLocales.data(), 1};
  }

  // Makes `c` the choice, once prepare() has made its configurations; `with`, if given, the
  // LocaleConfig it was made with, which setLocaleConfig has given since the choice before.
  void commitChoice(Choice& c, uint64_t revision, LocaleConfig* with) noexcept {
    configLocales.swap(c.configLocales);
    readIn.swap(c.readIn);
    if (with != nullptr) {
      chosenWith.supported.swap(with->supported);
      chosenWith.defaultLocale = with->defaultLocale;
      stale = false;
    }
    commit(defaultLocaleOf(chosenWith), revision);
  }

  // Takes in what the table has changed since the last call, as the packages it has added: what
  // was picked from it is gone, and the locales are chosen again, as if given afresh, with the
  // LocaleConfig of the choice before: one given since waits for setConfiguration, as on Android.
  void syncWithTable() {
    const uint64_t revision = table.revision();
    if (revision == tableRevision) return;
    Choice c = choose(locales, configLocales, /*changed=*/true, revision, chosenWith);
    prepare(c.readIn.data(), c.readIn.size(), device);
    commitChoice(c, revision, nullptr);
  }

  void clearCaches() {
    cache.clear();
    densityCaches.clear();
  }

  // AssetManager2::FindEntry: the value of `entry` for `matchConfigs` with the density `density`,
  // 0 being their own.
  const Candidate* find(const Entry& entry, uint16_t density) const {
    const Candidate* best = nullptr;
    bool bestHasLocale = false;
    for (const Config& config : matchConfigs) {
      Config overridden;
      const Config* desired = &config;
      if (density != 0 && density != config.density) {
        overridden = config;
        overridden.density = density;
        desired = &overridden;
      }
      const Candidate* found = bestCandidate(entry, *desired);
      // A locale of them for which no value matches has the resource found for none.
      if (found == nullptr) return nullptr;
      if (matchConfigs.size() == 1) return found;
      // A value without a locale is of the default locale, for the locale it is in.
      const bool hasLocale =
          found->config.lang[0] != '\0' || (defaultLocale && matches(*defaultLocale, config));
      if (best == nullptr || isBetterThanBeforeLocale(found->config, best->config, *desired) ||
          (!isBetterThanBeforeLocale(best->config, found->config, *desired) && hasLocale &&
           !bestHasLocale)) {
        best = found;
        bestHasLocale = hasLocale;
      }
    }
    return best;
  }

  // The value of `id` with the density `density`, 0 being their own, from the cache of that
  // density, where it is put the first time.
  const Candidate* pick(ResId id, uint16_t density) {
    const bool own = density == 0 || density == matchConfigs.front().density;
    std::unordered_map<ResId, const Candidate*>& kept = own ? cache : densityCaches[density];
    if (const auto it = kept.find(id); it != kept.end()) return it->second;
    const Entry* entry = table.entry(id);
    if (entry == nullptr) return nullptr;
    const Candidate* best = find(*entry, own ? 0 : density);
    kept.emplace(id, best);
    return best;
  }

  const ResourceTable& table;
  std::mutex mutex;             // guards all that follows
  uint64_t tableRevision;       // of the table as the choice, `configs` and `cache` were made of it
  Config device;                // as setConfiguration was given it
  std::vector<Config> locales;  // as setConfiguration was given them
  LocaleConfig localeConfig;    // as setLocaleConfig was given it
  LocaleConfig chosenWith;      // as it was when setConfiguration last chose
  // Whether setLocaleConfig has been called since setConfiguration last chose, which the next
  // takes it in.
  bool stale = false;
  std::vector<Config> configLocales;      // Android's configuration's, as the last choice left them
  std::vector<Config> readIn;             // of the last choice, which the resources are read in
  std::optional<Config> defaultLocale;    // defaultLocaleOf(localeConfig), as it was then
  std::vector<Config> configs;            // effectiveConfig of those read in, one at least
  std::vector<Config> matchConfigs;       // withScript of each, which the values are picked by
  std::vector<Config> spareConfigs;       // as prepare() made them, or those before `configs`
  std::vector<Config> spareMatchConfigs;  // as prepare() made them, or those before
                                          // `matchConfigs`
  uint32_t generation = 0;
  std::unordered_map<ResId, const Candidate*> cache;  // of `configs`, in the table as it is
  // As `cache`, of `configs` with another density, by the density.
  std::unordered_map<uint16_t, std::unordered_map<ResId, const Candidate*>> densityCaches;
  // The locales of the table at `tableLocalesRevision`, made when a choice first needs them.
  std::optional<TableLocales> tableLocales;
  uint64_t tableLocalesRevision = 0;
};

Resources::Resources(const ResourceTable& table) : impl_(std::make_unique<Impl>(table)) {}
Resources::~Resources() = default;
Resources::Resources(Resources&& other) noexcept = default;
Resources& Resources::operator=(Resources&& other) noexcept = default;

void Resources::setConfiguration(const Config& device, const std::vector<Config>& locales) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  // A table that has changed is taken in first, as a call that reads would, so that what follows
  // is the same whether one came before or not.
  impl_->syncWithTable();
  const uint64_t tableRevision = impl_->tableRevision;
  if (locales == impl_->locales && !impl_->stale) {
    // The device alone: nothing is chosen again, and nothing allocated (#240), but by the first
    // call that, as one locale was chosen of the configuration's with a default, reads in all of
    // them, which needs room for them. No vector is so much as made here, as an empty one
    // allocates in MSVC's Debug library.
    const auto [l, n] = impl_->sameAgain();
    impl_->prepare(l, n, device);
    impl_->device = device;
    impl_->commit(impl_->defaultLocale, tableRevision);
    return;
  }
  // As ResourcesImpl.updateConfiguration, which compares the locales with those of its
  // configuration, as the last update left them.
  Impl::Choice c = impl_->choose(locales, impl_->configLocales, /*changed=*/false, tableRevision,
                                 impl_->localeConfig);
  std::vector<Config> copy = locales;
  LocaleConfig with = impl_->localeConfig;
  impl_->prepare(c.readIn.data(), c.readIn.size(), device);
  impl_->locales.swap(copy);
  impl_->device = device;
  impl_->commitChoice(c, tableRevision, &with);
}

void Resources::setLocaleConfig(const LocaleConfig& config) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  // As ResourcesImpl.setLocaleConfig, which only keeps it for the next update.
  LocaleConfig copy = config;
  impl_->localeConfig.supported.swap(copy.supported);
  impl_->localeConfig.defaultLocale = copy.defaultLocale;
  impl_->stale = true;
}

Config Resources::configuration() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  return impl_->configs.front();
}

uint32_t Resources::generation() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  return impl_->generation;
}

const Candidate* Resources::candidate(ResId id, uint32_t* generation) const {
  return candidateForDensity(id, 0, generation);
}

const Candidate* Resources::candidateForDensity(ResId id, uint16_t density,
                                                uint32_t* generation) const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  if (generation != nullptr) *generation = impl_->generation;
  return impl_->pick(id, density);
}

}  // namespace hydroid::res

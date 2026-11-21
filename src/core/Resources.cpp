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
    // Nothing is known of the language: the regions must be the same, as before Android 7.
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

// Goes through every value of the table, so is made once for each revision of it.
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
// that the resources of a table of `available` locales are read in.
size_t chooseLocaleIndex(const std::vector<Config>& locales, const TableLocales& available) {
  const std::set<Locale>* supported = &available.app;
  std::set<Locale> all;
  if (isPseudoLocalesOnly(available.app)) {
    // No locales of the app's own, so the system's.
    all = available.app;
    all.insert(available.system.begin(), available.system.end());
    if (isPseudoLocalesOnly(all)) return 0;
    supported = &all;
  }
  std::vector<Locale> desired;
  desired.reserve(locales.size());
  for (const Config& locale : locales) desired.push_back(javaLocaleOf(locale));
  return computeFirstMatchIndex(desired, *supported);
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
    setConfig(effectiveConfig(device, nullptr));
  }

  void setConfig(const Config& c) {
    config = c;
    matchConfig = withScript(c);
  }

  // Chooses the one of `locales` the resources are read in, for the table at `revision`.
  void chooseLocale(uint64_t revision) {
    localeIndex = 0;
    if (locales.size() <= 1) return;
    if (!tableLocales || tableLocalesRevision != revision) {
      tableLocales = localesOf(table);
      tableLocalesRevision = revision;
    }
    localeIndex = chooseLocaleIndex(locales, *tableLocales);
  }

  // effectiveConfig of `device` and the locale chosen of `locales`.
  Config effective() const {
    return effectiveConfig(device, locales.empty() ? nullptr : &locales[localeIndex]);
  }

  // Takes in what the table has changed since the last call, as the packages it has added: what
  // was picked from it is gone, and the locale may be another.
  void syncWithTable() {
    if (table.revision() == tableRevision) return;
    tableRevision = table.revision();
    chooseLocale(tableRevision);
    setConfig(effective());
    cache.clear();
    generation++;
  }

  const ResourceTable& table;
  std::mutex mutex;             // guards all that follows
  uint64_t tableRevision;       // of the table as the choice, `config` and `cache` were made of it
  Config device;                // as setConfiguration was given it
  std::vector<Config> locales;  // as setConfiguration was given them
  size_t localeIndex = 0;       // in `locales`, of the one chosen
  Config config;                // effectiveConfig of those
  Config matchConfig;           // withScript(config), which the values are picked by
  uint32_t generation = 0;
  std::unordered_map<ResId, const Candidate*> cache;  // of `config`, in the table as it is
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
  const uint64_t tableRevision = impl_->table.revision();
  // As ResourcesImpl.updateConfigurationImpl, which asks for the locales of the table only when
  // the user's have changed: the locale is chosen again only when they, or the table, have.
  if (locales != impl_->locales || tableRevision != impl_->tableRevision) {
    impl_->locales = locales;
    impl_->chooseLocale(tableRevision);
  }
  impl_->device = device;
  const Config config = impl_->effective();
  if (config == impl_->config && tableRevision == impl_->tableRevision) return;
  impl_->tableRevision = tableRevision;
  impl_->setConfig(config);
  impl_->cache.clear();
  impl_->generation++;
}

Config Resources::configuration() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  return impl_->config;
}

uint32_t Resources::generation() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  return impl_->generation;
}

const Candidate* Resources::candidate(ResId id, uint32_t* generation) const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  if (generation != nullptr) *generation = impl_->generation;
  if (const auto it = impl_->cache.find(id); it != impl_->cache.end()) return it->second;
  const Entry* entry = impl_->table.entry(id);
  if (entry == nullptr) return nullptr;
  const Candidate* best = bestCandidate(*entry, impl_->matchConfig);
  impl_->cache.emplace(id, best);
  return best;
}

}  // namespace hydroid::res

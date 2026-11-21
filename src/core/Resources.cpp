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

// AssetManager2::GetResourceLocales, as AssetManager.getLocales and getNonSystemLocales have them:
// the locales the values of the table are for, with Tagalog as Filipino.
std::set<Locale> localesOf(const ResourceTable& table, bool excludeSystem) {
  std::set<Locale> locales;
  const Locale* last = nullptr;
  for (const Package& package : table.packages()) {
    if (excludeSystem && package.id == kFrameworkPackageId) continue;
    for (const TypeTable& type : package.types) {
      for (const Entry& entry : type.entries) {
        for (const Candidate& candidate : entry.candidates) {
          const Config& c = candidate.config;
          if (c.lang[0] == '\0') continue;
          const Locale locale = javaLocaleOf(c);
          // The values of a file follow each other, all of one locale.
          if (last != nullptr && *last == locale) continue;
          last = &*locales.insert(locale).first;
        }
      }
    }
  }
  if (locales.erase(Locale{kTl, {}, {}, {}}) != 0) locales.insert(Locale{kFil, {}, {}, {}});
  return locales;
}

// LocaleList.isPseudoLocalesOnly.
bool isPseudoLocalesOnly(const std::set<Locale>& locales) {
  for (const Locale& locale : locales) {
    if (!isPseudoLocale(locale)) return false;
  }
  return true;
}

// ResourcesImpl.updateConfiguration: the one of `locales`, which are not empty, that the
// resources of the table are read in.
const Config& chooseLocale(const std::vector<Config>& locales, const ResourceTable& table) {
  if (locales.size() == 1) return locales[0];
  std::set<Locale> available = localesOf(table, /*excludeSystem=*/true);
  if (isPseudoLocalesOnly(available)) {
    // No locales of the app's own, so the system's.
    available = localesOf(table, /*excludeSystem=*/false);
    if (isPseudoLocalesOnly(available)) return locales[0];
  }
  std::vector<Locale> desired;
  desired.reserve(locales.size());
  for (const Config& locale : locales) desired.push_back(javaLocaleOf(locale));
  return locales[computeFirstMatchIndex(desired, available)];
}

// The configuration setConfiguration makes of a device's and the user's locales.
Config effectiveConfig(const Config& device, const std::vector<Config>& locales,
                       const ResourceTable& table) {
  Config config = device;
  if (!locales.empty()) {
    const Config& locale = chooseLocale(locales, table);
    config.lang = locale.lang;
    config.region = locale.region;
    config.script = locale.script;
    config.variant = locale.variant;
  }
  config.lang = oldCode(config.lang);
  if (config.sdkVersion == 0 && config.minorVersion == 0) {
    config.sdkVersion = static_cast<uint16_t>(kEmulatedSdkVersion);
  }
  return config;
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
  explicit Impl(const ResourceTable& t) : table(t), packageCount(t.packages().size()) {
    config = effectiveConfig(device, locales, table);
  }

  // Takes in the packages the table has added since the last call: what was picked from it is
  // gone, and the locale may be another.
  void syncWithTable() {
    if (table.packages().size() == packageCount) return;
    packageCount = table.packages().size();
    config = effectiveConfig(device, locales, table);
    cache.clear();
    generation++;
  }

  const ResourceTable& table;
  std::mutex mutex;  // guards all that follows
  // The table only ever adds packages, so their number tells when it has.
  size_t packageCount;
  Config device;                // as setConfiguration was given it
  std::vector<Config> locales;  // as setConfiguration was given them
  Config config;                // effectiveConfig of those
  uint32_t generation = 0;
  std::unordered_map<ResId, const Candidate*> cache;  // of `config`, in the table as it is
};

Resources::Resources(const ResourceTable& table) : impl_(std::make_unique<Impl>(table)) {}
Resources::~Resources() = default;
Resources::Resources(Resources&& other) noexcept = default;
Resources& Resources::operator=(Resources&& other) noexcept = default;

void Resources::setConfiguration(const Config& device, const std::vector<Config>& locales) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  const size_t packageCount = impl_->table.packages().size();
  const Config config = effectiveConfig(device, locales, impl_->table);
  impl_->device = device;
  impl_->locales = locales;
  if (config == impl_->config && packageCount == impl_->packageCount) return;
  impl_->packageCount = packageCount;
  impl_->config = config;
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

const Candidate* Resources::candidate(ResId id) const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->syncWithTable();
  if (const auto it = impl_->cache.find(id); it != impl_->cache.end()) return it->second;
  const Entry* entry = impl_->table.entry(id);
  if (entry == nullptr) return nullptr;
  const Candidate* best = bestCandidate(*entry, impl_->config);
  impl_->cache.emplace(id, best);
  return best;
}

}  // namespace hydroid::res

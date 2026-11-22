#include "core/LocaleData.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace hydroid::res::detail {
namespace {

#include "core/LocaleDataTables.inc"

static_assert(std::size(kLikelyScriptLocales) == std::size(kLikelyScripts));

constexpr uint32_t kPackedRoot = 0;  // the root locale

uint32_t byte(char c) { return static_cast<unsigned char>(c); }

// android::packLocale.
uint32_t packLocale(const LocaleCode& language, const LocaleCode& region) {
  return byte(language[0]) << 24 | byte(language[1]) << 16 | byte(region[0]) << 8 | byte(region[1]);
}
uint32_t packScript(const ScriptCode& s) {
  return byte(s[0]) << 24 | byte(s[1]) << 16 | byte(s[2]) << 8 | byte(s[3]);
}
uint32_t dropRegion(uint32_t locale) { return locale & 0xffff0000u; }
bool hasRegion(uint32_t locale) { return (locale & 0x0000ffffu) != 0; }

// lookupLikelyScript.
const char* lookupLikelyScript(uint32_t locale) {
  const auto it =
      std::lower_bound(std::begin(kLikelyScriptLocales), std::end(kLikelyScriptLocales), locale);
  if (it == std::end(kLikelyScriptLocales) || *it != locale) return nullptr;
  return kScriptCodes[kLikelyScripts[it - std::begin(kLikelyScriptLocales)]];
}

// isLocaleRepresentative.
bool isLocaleRepresentative(uint32_t locale, const ScriptCode& script) {
  const uint64_t key = uint64_t{locale} << 32 | packScript(script);
  return std::binary_search(std::begin(kRepresentativeLocales), std::end(kRepresentativeLocales),
                            key);
}

// findParentLocalePackedKey: 0 where the tables give the locale no parent.
uint32_t findParentLocalePackedKey(const ScriptCode& script, uint32_t locale) {
  const uint32_t packedScript = packScript(script);
  for (const ScriptParents& table : kScriptParents) {
    if (table.script != packedScript) continue;
    const LocaleParent* end = table.parents + table.size;
    const LocaleParent* it = std::lower_bound(
        table.parents, end, locale,
        [](const LocaleParent& entry, uint32_t key) { return entry.locale < key; });
    return it != end && it->locale == locale ? it->parent : 0;
  }
  return 0;
}

uint32_t findParent(uint32_t locale, const ScriptCode& script) {
  if (hasRegion(locale)) {
    const uint32_t parent = findParentLocalePackedKey(script, locale);
    if (parent != 0) return parent;
    return dropRegion(locale);
  }
  return kPackedRoot;
}

// A locale and its ancestors up to the root, which the tables keep within kMaxAncestorTreeDepth.
using Ancestors = std::array<uint32_t, kMaxAncestorTreeDepth + 1>;

// findAncestors: writes `locale` and its ancestors to `out` if it is not null, stopping after one
// that is in `stopList`, whose index there goes to `stopListIndex`, or -1 if none was met.
// Returns how many there were, which is at least one.
size_t findAncestors(Ancestors* out, ptrdiff_t* stopListIndex, uint32_t locale,
                     const ScriptCode& script, const uint32_t* stopList, size_t stopListSize) {
  uint32_t ancestor = locale;
  size_t count = 0;
  do {
    if (out != nullptr && count < out->size()) (*out)[count] = ancestor;
    count++;
    for (size_t i = 0; i < stopListSize; i++) {
      if (stopList[i] == ancestor) {
        *stopListIndex = static_cast<ptrdiff_t>(i);
        return count;
      }
    }
    ancestor = findParent(ancestor, script);
  } while (ancestor != kPackedRoot);
  *stopListIndex = -1;
  return count;
}

// findDistance: how far `supported` is from the request in the tree, through the lowest ancestor
// they share: the ancestors of `supported` up to it, and its place among the request's.
size_t findDistance(uint32_t supported, const ScriptCode& script, const Ancestors& requestAncestors,
                    size_t requestAncestorsCount) {
  ptrdiff_t requestAncestorsIndex = -1;
  const size_t supportedAncestorCount =
      findAncestors(nullptr, &requestAncestorsIndex, supported, script, requestAncestors.data(),
                    std::min(requestAncestorsCount, requestAncestors.size()));
  return supportedAncestorCount + static_cast<size_t>(requestAncestorsIndex) - 1;
}

constexpr uint32_t kUsSpanish = 0x65735553u;             // es-US
constexpr uint32_t kMexicanSpanish = 0x65734D58u;        // es-MX
constexpr uint32_t kLatinAmericanSpanish = 0x6573A424u;  // es-419

// es-US and es-MX stand in for es-419 where there is no es-419.
bool isSpecialSpanish(uint32_t locale) { return locale == kUsSpanish || locale == kMexicanSpanish; }

constexpr uint32_t kEnglishStopList[2] = {
    0x656E0000u,  // en
    0x656E8400u,  // en-001
};

}  // namespace

ScriptCode computeScript(const LocaleCode& language, const LocaleCode& region) {
  ScriptCode out{};
  if (language[0] == '\0') return out;
  uint32_t key = packLocale(language, region);
  const char* found = lookupLikelyScript(key);
  if (found == nullptr && region[0] != '\0') {
    // Not with this region: the language anywhere.
    key = dropRegion(key);
    found = lookupLikelyScript(key);
  }
  if (found != nullptr) std::copy(found, found + out.size(), out.begin());
  return out;
}

int compareRegions(const LocaleCode& leftRegion, const LocaleCode& rightRegion,
                   const LocaleCode& requestedLanguage, const ScriptCode& requestedScript,
                   const LocaleCode& requestedRegion) {
  if (leftRegion == rightRegion) return 0;
  uint32_t left = packLocale(requestedLanguage, leftRegion);
  uint32_t right = packLocale(requestedLanguage, rightRegion);
  const uint32_t request = packLocale(requestedLanguage, requestedRegion);

  // One special Spanish locale, and only one, is taken for es-419, unless the other is es-419.
  const bool leftIsSpecialSpanish = isSpecialSpanish(left);
  const bool rightIsSpecialSpanish = isSpecialSpanish(right);
  if (leftIsSpecialSpanish && !rightIsSpecialSpanish && right != kLatinAmericanSpanish) {
    left = kLatinAmericanSpanish;
  } else if (rightIsSpecialSpanish && !leftIsSpecialSpanish && left != kLatinAmericanSpanish) {
    right = kLatinAmericanSpanish;
  }

  // The ancestors of the request, up to the first that is left or right.
  Ancestors requestAncestors{};
  ptrdiff_t leftRightIndex = -1;
  const std::array<uint32_t, 2> leftAndRight = {left, right};
  const size_t ancestorCount =
      findAncestors(&requestAncestors, &leftRightIndex, request, requestedScript,
                    leftAndRight.data(), leftAndRight.size());
  if (leftRightIndex == 0) return 1;   // left came first
  if (leftRightIndex == 1) return -1;  // right came first

  // Neither is an ancestor of the request, whose last ancestor is then the language alone: the
  // one nearer in the tree is the better.
  const size_t leftDistance = findDistance(left, requestedScript, requestAncestors, ancestorCount);
  const size_t rightDistance =
      findDistance(right, requestedScript, requestAncestors, ancestorCount);
  if (leftDistance != rightDistance) {
    return static_cast<int>(rightDistance) - static_cast<int>(leftDistance);
  }

  // As far as each other: the one that stands for the language in this script, if one does.
  const bool leftIsRepresentative = isLocaleRepresentative(left, requestedScript);
  const bool rightIsRepresentative = isLocaleRepresentative(right, requestedScript);
  if (leftIsRepresentative != rightIsRepresentative) {
    return static_cast<int>(leftIsRepresentative) - static_cast<int>(rightIsRepresentative);
  }

  // Nothing tells them apart. To be stable, the lower region code is the better, with two letters
  // before three digits.
  return static_cast<int>(int64_t{right} - int64_t{left});
}

bool isCloseToUsEnglish(const LocaleCode& region) {
  const uint32_t locale = packLocale({'e', 'n'}, region);
  ptrdiff_t stopListIndex = -1;
  findAncestors(nullptr, &stopListIndex, locale, {'L', 'a', 't', 'n'}, kEnglishStopList,
                std::size(kEnglishStopList));
  // "en" is met before en-001 among its ancestors.
  return stopListIndex == 0;
}

}  // namespace hydroid::res::detail

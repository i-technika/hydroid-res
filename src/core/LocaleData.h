#pragma once

#include <array>

// What Android knows of locales beyond their letters: a port of AOSP's LocaleData.cpp
// (frameworks/base/libs/androidfw, android17-release) over the tables of its LocaleDataLookup.cpp,
// which scripts/gen_locale_data.py writes to LocaleDataTables.inc. A language or region is the two
// bytes ResTable_config keeps (LocalePack.h), and a script its four letters, all NULs for none.

namespace hydroid::res::detail {

using LocaleCode = std::array<char, 2>;
using ScriptCode = std::array<char, 4>;

// localeDataComputeScript: the script `language` is most likely written in in `region`, or
// anywhere if the tables say nothing of that region; none for a language they do not have.
ScriptCode computeScript(const LocaleCode& language, const LocaleCode& region);

// localeDataCompareRegions: which of two regions of the requested language is closer to the
// requested region, in the tree of each locale's parent (es-MX under es-419 under es): > 0 for
// `left`, < 0 for `right`, 0 for the same region.
int compareRegions(const LocaleCode& left, const LocaleCode& right,
                   const LocaleCode& requestedLanguage, const ScriptCode& requestedScript,
                   const LocaleCode& requestedRegion);

// localeDataIsCloseToUsEnglish: whether the English of `region` descends from "en" without
// passing through en-001, the international English.
bool isCloseToUsEnglish(const LocaleCode& region);

}  // namespace hydroid::res::detail

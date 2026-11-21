#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "hydroid/res/Config.h"
#include "hydroid/res/Export.h"

// Resource folder names, as aapt2 reads them (ConfigDescription::Parse). golden/aapt2/configs.tsv
// holds what aapt2 itself makes of some 6000 names, every folder of AOSP's framework res/ among
// them, and Aapt2ConfigTest checks these parsers against it.
//
// The rules, all aapt2's:
//   - The name is split on '-' and each part lowercased. The parts must come in the order of
//     Config's fields, at most one for each qualifier; an empty part fails the name.
//   - "any" stands for any value in the place of every qualifier but the grammatical gender, and
//     sets nothing. In the place of the locale it is not a wildcard but the language "any", so
//     "any-any-any" is mcc any, mnc any, language "any".
//   - In the place of the locale, any 2 or 3 letters but "car" are a language, even ones that
//     look like a later qualifier: "dpi" alone is the language dpi, and so is "mcc", as an mcc
//     needs its 3 digits.
//   - In the b+ form, the subtags are told apart by their count and length. Of four, a second
//     of 2 or 3 characters is the region and the two after it are dropped, so b+de+DE+1901+1996
//     is de-rDE; any other second is the script, before a region and a variant. Five fail.
//   - The version is raised to the first one that knows a qualifier, as the runtime of an older
//     Android would otherwise take the folder for one it knows: hdpi is hdpi-v4, sw600dp
//     sw600dp-v13, anydpi anydpi-v21, round round-v23, widecg widecg-v26, feminine
//     feminine-v34. Raising it drops a minor version (anydpi-v4.1 is anydpi-v21).
//   - A minor version needs v36 or later after that ("v35.1" fails), and v0 with a minor fails.
//
// On purpose, these reject some names that aapt2 takes, where aapt2 cuts a part short, wraps a
// number or packs a locale into bytes that no longer say what was written:
//   - Numbers past 65535 in sw<N>dp, w<N>dp, h<N>dp, <N>dpi and <W>x<H>, which aapt2 wraps to 16
//     bits: aapt2 reads sw65536dp as sw0dp, which is no qualifier at all, and sw65537dp as
//     sw1dp, a real one other than what was written. Past 2147483647 it depends on the host, as
//     aapt2 reads the number with atoi. Build-tools 37.0.0 on Windows first stops it at
//     0x7fffffff, so sw2147483648dp, w99999999999dp and h2147483648dp are sw65535dp, w65535dp
//     and h65535dp, and 2147483648dpi is nodpi. 37.0.0 on Linux and macOS wraps it to 32 bits
//     instead: sw2147483648dp and 2147483648dpi are the default Config, sw99999999999dp is
//     sw59391dp, and 2147483648x1 fails. 36.1.0 on Windows wrapped too, sw99999999999dp and
//     99999999999dpi to 59391. The aapt2 golden comes from the build-tools and OS that
//     BUILD_TOOLS and GOLDEN_HOST in scripts/golden/gen_aapt2_values.py pin.
//     Versions past 65535 fail in aapt2 too.
//   - In the b+ form: an empty subtag ("b+", "b+sr+Latn+RS+"), a language that is not 2 or 3
//     letters ("b+e" comes out "e" and a NUL, "b+english" "eng"), a region of 3 letters (aapt2
//     packs region letters as 5 bits above '0': "b+en+xyz" comes out "en-r89;"), a region of 1
//     character, a script that is not 4 characters, or a variant longer than 8.
//   - In either form, a locale with a character other than an ASCII letter or digit, such as
//     "en-r+x", which aapt2 keeps as the region "+X".

namespace hydroid::res {

// "ko-rKR", "land-hdpi": the qualifiers of a folder name, the part after its type and '-'. ""
// is the default Config.
HYDROID_RES_API std::optional<Config> parseConfig(std::string_view qualifiers);

struct ResDir {
  std::string type;  // "values", "drawable": what comes before the first '-', as written
  Config config;
};

// "values-ko-rKR" -> {"values", ko-rKR}, "drawable" -> {"drawable", default}. The type is not
// checked against the types Android knows; the qualifiers are, by parseConfig. A name ending in
// '-' has no qualifiers, as in aapt2: "values-" is the default Config.
HYDROID_RES_API std::optional<ResDir> parseResDir(std::string_view name);

// The locale of a BCP 47 language tag, which is how a platform names the user's languages:
// "ko", "ko-KR", "sr-Latn-RS", "es-419", "de-DE-1901". The Config has the language in
// lowercase, the script as "Latn", the region in uppercase and the variant in lowercase, and
// nothing else. The subtags must be a language of 2 or 3 letters, then at most one script of 4
// letters, at most one region of 2 letters or 3 digits, and variants of 5 to 8 letters or
// digits, or of 4 starting with a digit. Of several variants, each is written over the front of
// the one before, as ResTable_config::setBcp47Locale does: "sl-rozaj-biske" has biske, and
// "ca-ES-valencia-1996" has "1996ncia". Extensions and private use, from a subtag of one
// character on, are left out ("ar-EG-u-nu-latn" is ar-EG; see Config.h on the numbering
// system). nullopt for any other tag.
HYDROID_RES_API std::optional<Config> parseLanguageTag(std::string_view tag);

}  // namespace hydroid::res

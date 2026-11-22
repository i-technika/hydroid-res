#pragma once

#include <array>
#include <cstdint>

// A language or a region in the two bytes ResTable_config keeps it in, which Android compares
// (compareConfigs) and looks locales up by (LocaleData.h).

namespace hydroid::res::detail {

// ResTable_config::packLanguageOrRegion: two characters as they are, three packed in 5 bits each
// above `base` ('a' for a language, '0' for a region) with the high bit of the first byte set.
inline std::array<char, 2> packLanguageOrRegion(const std::array<char, 4>& in, char base) {
  if (in[2] == '\0') return {in[0], in[1]};
  const auto first = static_cast<uint8_t>((in[0] - base) & 0x7f);
  const auto second = static_cast<uint8_t>((in[1] - base) & 0x7f);
  const auto third = static_cast<uint8_t>((in[2] - base) & 0x7f);
  return {static_cast<char>(0x80 | (third << 2) | (second >> 3)),
          static_cast<char>((second << 5) | first)};
}

inline std::array<char, 2> packLanguage(const std::array<char, 4>& lang) {
  return packLanguageOrRegion(lang, 'a');
}
inline std::array<char, 2> packRegion(const std::array<char, 4>& region) {
  return packLanguageOrRegion(region, '0');
}

}  // namespace hydroid::res::detail

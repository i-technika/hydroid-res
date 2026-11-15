#pragma once

#include <cstdint>
#include <string_view>

// The whitespace aapt2 trims and collapses with, for every parser here to share (#89): isspace in
// the C locale, whatever locale the host app set, and never a non-ASCII space such as U+00A0.

namespace hydroid::res::detail {

// A code point, or a byte read as unsigned char: space, \t, \n, \v, \f or \r.
inline bool isSpace(uint32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

inline std::string_view trimLeading(std::string_view s) {
  while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  return s;
}

inline std::string_view trimTrailing(std::string_view s) {
  while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

inline std::string_view trim(std::string_view s) { return trimTrailing(trimLeading(s)); }

}  // namespace hydroid::res::detail

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// The UTF-8 of a path, which never throws. On Windows a path is UTF-16, which may hold what is not
// Unicode text, a lone surrogate, and u8string() throws on it (#111). Elsewhere a path is bytes,
// which u8string() gives as they are.

namespace hydroid::res::detail {

#ifdef _WIN32
// Each UTF-16 unit of `w`, a surrogate pair read as one code point, with `lone` for a lone
// surrogate.
template <typename F>
void forEachCodePoint(const std::wstring& w, F f) {
  for (size_t i = 0; i < w.size(); i++) {
    uint32_t c = w[i];
    if (c >= 0xd800 && c <= 0xdbff && i + 1 < w.size() && w[i + 1] >= 0xdc00 &&
        w[i + 1] <= 0xdfff) {
      c = 0x10000 + ((c - 0xd800) << 10) + (static_cast<uint32_t>(w[++i]) - 0xdc00);
      f(c, false);
    } else {
      f(c, c >= 0xd800 && c <= 0xdfff);
    }
  }
}
#endif

// Whether `p` is Unicode text, and so has a UTF-8 that names it.
inline bool isUnicode(const std::filesystem::path& p) {
#ifdef _WIN32
  bool unicode = true;
  forEachCodePoint(p.native(), [&](uint32_t, bool lone) { unicode = unicode && !lone; });
  return unicode;
#else
  (void)p;
  return true;
#endif
}

// The UTF-8 of `p`, with U+FFFD for a lone surrogate, which has none.
inline std::string utf8(const std::filesystem::path& p) {
#ifdef _WIN32
  std::string out;
  forEachCodePoint(p.native(), [&](uint32_t c, bool lone) {
    if (lone) c = 0xfffd;
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xc0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xe0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (c & 0x3f));
    } else {
      out += static_cast<char>(0xf0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (c & 0x3f));
    }
  });
  return out;
#else
  // u8string() returns std::string in C++17 and std::u8string in C++20.
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
#endif
}

}  // namespace hydroid::res::detail

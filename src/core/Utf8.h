#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Reads the code points of UTF-8 text, as the values parser reads a string. expat rejects a file
// that is not UTF-8 before the parser sees it (#78), so this is the check for what reaches the
// parser some other way (#79).

namespace hydroid::res::detail {

// A byte that starts no valid sequence reads as itself and marks the text invalid: a byte
// 0x80-0xbf or 0xf8-0xff where a sequence should start, one cut short, an overlong form (C0 A0
// for a space), a surrogate (ED A0 80) or a code point past U+10FFFF (F4 90 80 80).
class Utf8Reader {
 public:
  explicit Utf8Reader(std::string_view s) : s_(s) {}
  bool hasNext() const { return pos_ < s_.size(); }
  bool invalid() const { return invalid_; }
  uint32_t next() {
    const auto lead = static_cast<unsigned char>(s_[pos_++]);
    if (lead < 0x80) return lead;
    // The bytes that follow, and the least code point that needs them, below which it is overlong.
    int more = 0;
    uint32_t least = 0;
    if (lead >= 0xc0 && lead < 0xe0) {
      more = 1;
      least = 0x80;
    } else if (lead >= 0xe0 && lead < 0xf0) {
      more = 2;
      least = 0x800;
    } else if (lead >= 0xf0 && lead < 0xf8) {
      more = 3;
      least = 0x10000;
    } else {
      invalid_ = true;
      return lead;
    }
    const size_t start = pos_;
    uint32_t cp = lead & (0x3fu >> more);
    for (int i = 0; i < more; i++) {
      if (pos_ >= s_.size() || (static_cast<unsigned char>(s_[pos_]) & 0xc0) != 0x80) {
        invalid_ = true;
        pos_ = start;
        return lead;
      }
      cp = (cp << 6) | (static_cast<unsigned char>(s_[pos_++]) & 0x3f);
    }
    if (cp < least || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
      invalid_ = true;
      pos_ = start;
      return lead;
    }
    return cp;
  }

 private:
  std::string_view s_;
  size_t pos_ = 0;
  bool invalid_ = false;
};

}  // namespace hydroid::res::detail

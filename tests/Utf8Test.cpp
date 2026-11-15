// Utf8Reader, which the values parser reads strings with: every valid sequence reads as its code
// point, and everything else marks the text invalid, which fails the string (#79). expat rejects
// such a file first, so the table tests never reach the reader with it.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/Utf8.h"
#include "core/ValuesParser.h"

namespace hydroid::res::detail {
namespace {

std::vector<uint32_t> codePoints(std::string_view bytes, bool* invalid) {
  Utf8Reader in(bytes);
  std::vector<uint32_t> out;
  while (in.hasNext()) out.push_back(in.next());
  *invalid = in.invalid();
  return out;
}

TEST(Utf8Reader, ReadsEveryLength) {
  // The least and the greatest of each length, and either side of the surrogates.
  using namespace std::string_literals;
  const std::vector<std::pair<std::string, uint32_t>> cases = {
      {"\x00"s, 0x0},
      {"\x7f", 0x7f},
      {"\xc2\x80", 0x80},
      {"\xdf\xbf", 0x7ff},
      {"\xe0\xa0\x80", 0x800},
      {"\xed\x9f\xbf", 0xd7ff},
      {"\xee\x80\x80", 0xe000},
      {"\xef\xbf\xbf", 0xffff},
      {"\xf0\x90\x80\x80", 0x10000},
      {"\xf4\x8f\xbf\xbf", 0x10ffff},
  };
  for (const auto& [bytes, cp] : cases) {
    bool invalid = true;
    EXPECT_EQ(codePoints(bytes, &invalid), std::vector<uint32_t>{cp}) << std::hex << cp;
    EXPECT_FALSE(invalid) << std::hex << cp;
  }
}

TEST(Utf8Reader, MarksWhatIsNotUtf8) {
  const std::vector<std::string> cases = {
      // A byte that starts no sequence: a continuation byte, 0xf8-0xff (each followed by the two
      // bytes it was read with as a three-byte lead before #79).
      "\x80", "\xbf", "\xf8\x80\x80", "\xfb\x80\x80", "\xfc\x80\x80", "\xfe\x80\x80",
      "\xff\x80\x80",
      // Cut short.
      "\xc2", "\xe0\xa0", "\xf0\x90\x80", "\xe0\xa0z",
      // Overlong: C0 and C1 always, and the shortest of each longer length.
      "\xc0\xa0", "\xc1\xbf", "\xe0\x80\xa0", "\xe0\x9f\xbf", "\xf0\x80\x80\xa0",
      "\xf0\x8f\xbf\xbf",
      // Surrogates.
      "\xed\xa0\x80", "\xed\xbf\xbf",
      // Past U+10FFFF, F5-F7 always.
      "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xf7\xbf\xbf\xbf"};
  for (const std::string& bytes : cases) {
    bool invalid = false;
    codePoints(bytes, &invalid);
    std::ostringstream hex;
    for (unsigned char b : bytes) hex << ' ' << std::hex << static_cast<int>(b);
    EXPECT_TRUE(invalid) << "bytes" << hex.str();
  }
}

TEST(Utf8Reader, InvalidByteReadsAsItself) {
  // F8 no longer reads as a three-byte sequence (U+8000), nor C0 A0 as a space.
  bool invalid = false;
  EXPECT_EQ(codePoints("a\xf8\x80\x80"
                       "b",
                       &invalid),
            (std::vector<uint32_t>{'a', 0xf8, 0x80, 0x80, 'b'}));
  EXPECT_TRUE(invalid);
  EXPECT_EQ(codePoints("a\xc0\xa0"
                       "b",
                       &invalid),
            (std::vector<uint32_t>{'a', 0xc0, 0xa0, 'b'}));
  EXPECT_TRUE(invalid);
}

// A shared hydroid-res exports no detail:: function to link against.
#ifndef HYDROID_RES_SHARED
TEST(BuildString, RejectsWhatIsNotUtf8) {
  // The issue's kinds: a lead byte 0xf8-0xff, C0 and F5-F7, overlong, a surrogate, past U+10FFFF.
  for (const std::string bytes : {"\xf8\x80\x80", "\xc0\xa0", "\xf5\x80\x80\x80", "\xe0\x80\xa0",
                                  "\xed\xa0\x80", "\xf4\x90\x80\x80"}) {
    std::string error;
    EXPECT_EQ(buildString("a" + bytes + "b", &error), std::nullopt);
    EXPECT_EQ(error, "invalid unicode code point in string");
  }
  std::string error;
  EXPECT_EQ(buildString("a\xf0\x9f\x98\x80"
                        "b",
                        &error),
            "a\xf0\x9f\x98\x80"
            "b");
}
#endif

}  // namespace
}  // namespace hydroid::res::detail

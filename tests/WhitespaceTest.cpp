// core/Whitespace.h, the one whitespace that the value parser, the values parser and the table
// builder all trim with (#89): isspace in the C locale, and no other space.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/Whitespace.h"

namespace hydroid::res::detail {
namespace {

TEST(Whitespace, IsTheCLocalesIsspace) {
  for (uint32_t c = 0; c < 0x80; c++) {
    const bool expected = c == ' ' || (c >= '\t' && c <= '\r');
    EXPECT_EQ(isSpace(c), expected) << c;
  }
  // Not NEL, NBSP, an ideographic space, nor a code point whose low byte is a space.
  for (uint32_t c : {0x85u, 0xa0u, 0x3000u, 0x120u, 0x109u}) EXPECT_FALSE(isSpace(c)) << c;
}

TEST(Whitespace, Trims) {
  const std::string s = " \t\n\v\f\ra b\r\f\v\n\t ";
  EXPECT_EQ(trimLeading(s), "a b\r\f\v\n\t ");
  EXPECT_EQ(trimTrailing(s), " \t\n\v\f\ra b");
  EXPECT_EQ(trim(s), "a b");
  EXPECT_EQ(trim(" \t "), "");
  EXPECT_EQ(trim(""), "");
  // A UTF-8 NBSP (C2 A0) is kept, as aapt2 keeps it.
  EXPECT_EQ(trim("\xc2\xa0x\xc2\xa0"), "\xc2\xa0x\xc2\xa0");
}

}  // namespace
}  // namespace hydroid::res::detail

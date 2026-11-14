// hydroid's complex encoding against aapt2's for thousands of inputs: golden/aapt2/complex.tsv,
// written by scripts/golden/gen_aapt2_complex.py from the SDK's aapt2.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

TEST(Aapt2Complex, EncodesLikeAapt2) {
  std::ifstream in(HYDROID_AAPT2_COMPLEX);
  ASSERT_TRUE(in) << "cannot open " << HYDROID_AAPT2_COMPLEX;
  size_t rows = 0;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    // text \t dimen|fraction \t 0xdata
    const size_t tab1 = line.find('\t');
    const size_t tab2 = line.find('\t', tab1 + 1);
    ASSERT_NE(tab2, std::string::npos) << "bad row: " << line;
    const std::string text = line.substr(0, tab1);
    const std::string type = line.substr(tab1 + 1, tab2 - tab1 - 1);
    const auto data = static_cast<uint32_t>(std::strtoul(line.c_str() + tab2 + 1, nullptr, 16));
    rows++;

    auto v = parseFloatValue(text);
    if (!v) {
      ADD_FAILURE() << '"' << text << "\" rejected, aapt2 wrote 0x" << std::hex << data;
      continue;
    }
    EXPECT_EQ(v->kind, type == "fraction" ? ValueKind::Fraction : ValueKind::Dimension)
        << '"' << text << '"';
    EXPECT_EQ(v->complex, data) << '"' << text << "\": hydroid 0x" << std::hex << v->complex
                                << ", aapt2 0x" << data;
  }
  EXPECT_GT(rows, 2000u);
}

}  // namespace
}  // namespace hydroid::res

// hydroid's literal parsers against aapt2 for thousands of inputs: golden/aapt2/values.tsv,
// written by scripts/golden/gen_aapt2_values.py from the SDK's aapt2.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

// Res_value types.
constexpr int kTypeReference = 0x01;
constexpr int kTypeFloat = 0x04;
constexpr int kTypeDimension = 0x05;
constexpr int kTypeFraction = 0x06;
constexpr int kTypeIntDec = 0x10;
constexpr int kTypeIntHex = 0x11;
constexpr int kTypeIntBoolean = 0x12;

// toResValue writes these as the Res_value type, as it does ColorFormat.
static_assert(static_cast<int>(IntFormat::Dec) == kTypeIntDec);
static_assert(static_cast<int>(IntFormat::Hex) == kTypeIntHex);

using ResValue = std::pair<int, uint32_t>;  // type, data

struct Row {
  std::string tag;
  std::string text;
  std::optional<ResValue> aapt2;  // nullopt: aapt2 found no literal
};

std::vector<Row> loadRows() {
  std::ifstream in(HYDROID_AAPT2_VALUES);
  EXPECT_TRUE(in) << "cannot open " << HYDROID_AAPT2_VALUES;
  std::vector<Row> rows;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    // tag \t text \t 0xtype|- \t 0xdata|-; the text may be empty or have spaces at either end.
    const size_t tab1 = line.find('\t');
    const size_t tab2 = line.find('\t', tab1 + 1);
    const size_t tab3 = line.find('\t', tab2 + 1);
    if (tab3 == std::string::npos) {
      ADD_FAILURE() << "bad row: " << line;
      continue;
    }
    Row row{line.substr(0, tab1), line.substr(tab1 + 1, tab2 - tab1 - 1), std::nullopt};
    if (line.compare(tab2 + 1, tab3 - tab2 - 1, "-") != 0) {
      row.aapt2 =
          ResValue{static_cast<int>(std::strtol(line.c_str() + tab2 + 1, nullptr, 16)),
                   static_cast<uint32_t>(std::strtoul(line.c_str() + tab3 + 1, nullptr, 16))};
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

const std::vector<Row>& rows() {
  static const std::vector<Row> r = loadRows();
  return r;
}

// The formats aapt2 tries for each tag (ResourceParser's elementToTypeMap; "any" is an <item>
// with no format).
uint32_t formatsOf(const std::string& tag) {
  static const std::map<std::string, uint32_t> kFormats = {
      {"color", kFormatColor},
      {"bool", kFormatBoolean},
      {"integer", kFormatInteger},
      {"dimen", kFormatFloat | kFormatDimension | kFormatFraction},
      {"fraction", kFormatFloat | kFormatDimension | kFormatFraction},
      {"any", kFormatAny},
  };
  auto it = kFormats.find(tag);
  return it == kFormats.end() ? 0 : it->second;
}

// The Res_value aapt2 would write for `v`.
ResValue toResValue(const Value& v) {
  switch (v.kind()) {
    case ValueKind::Color: return {static_cast<int>(v.colorFormat()), v.argb()};
    case ValueKind::Int:
      return {static_cast<int>(v.intFormat()), static_cast<uint32_t>(v.intValue())};
    case ValueKind::Bool: return {kTypeIntBoolean, v.boolValue() ? 0xffffffffu : 0u};
    case ValueKind::Dimension: return {kTypeDimension, v.complexData()};
    case ValueKind::Fraction: return {kTypeFraction, v.complexData()};
    case ValueKind::Float: {
      const float f = v.floatValue();
      uint32_t bits;
      std::memcpy(&bits, &f, sizeof bits);
      return {kTypeFloat, bits};
    }
    default: return {-1, 0};
  }
}

std::string describe(const std::optional<ResValue>& v) {
  if (!v) return "no literal";
  char buf[32];
  std::snprintf(buf, sizeof buf, "t=0x%02x d=0x%08x", v->first, v->second);
  return buf;
}

// Text aapt2 takes and hydroid rejects on purpose, and the Res_value aapt2 writes for each; see the
// top of ValueParser.h. Every one must still be in the golden file as aapt2 wrote it then, and
// rejected by hydroid, so the list cannot go stale. SdkBaseline takes any build-tools of the
// level, so a golden made with another that writes one of these otherwise fails here too (#61).
// clang-format off
const std::map<std::pair<std::string, std::string>, std::string> kDeliberateRejects = {
    // Infinity and NaN through strtof.
    {{"dimen", "-inf"}, "t=0x04 d=0xff800000"}, {{"dimen", "+inf"}, "t=0x04 d=0x7f800000"},
    {{"dimen", "-nan"}, "t=0x04 d=0xffc00000"}, {{"dimen", "+nan"}, "t=0x04 d=0x7fc00000"},
    {{"dimen", "-infinity"}, "t=0x04 d=0xff800000"},
    {{"dimen", "+Infinity"}, "t=0x04 d=0x7f800000"}, {{"dimen", "-INF"}, "t=0x04 d=0xff800000"},
    {{"dimen", "-NaN"}, "t=0x04 d=0xffc00000"},
    {{"dimen", "-nan(1)"}, "t=0x04 d=0xff800001"}, {{"dimen", "-infdp"}, "t=0x05 d=0x00000001"},
    {{"dimen", "+nan%"}, "t=0x06 d=0x00000000"}, {{"fraction", "-inf"}, "t=0x04 d=0xff800000"},
    // Hex floats.
    {{"dimen", "0x10"}, "t=0x04 d=0x41800000"}, {{"dimen", "0X10"}, "t=0x04 d=0x41800000"},
    {{"dimen", "0x10px"}, "t=0x05 d=0x00001000"}, {{"dimen", "-0x10px"}, "t=0x05 d=0xfffff000"},
    {{"dimen", "0x1p4"}, "t=0x04 d=0x41800000"}, {{"dimen", "0x1.8p1dp"}, "t=0x05 d=0x00000301"},
    {{"dimen", "0x.8"}, "t=0x04 d=0x3f000000"}, {{"fraction", "0x10%"}, "t=0x06 d=0x147ae130"},
    {{"any", "-0x10"}, "t=0x04 d=0xc1800000"}, {{"any", "0X10"}, "t=0x04 d=0x41800000"},
    {{"any", "0x100000000"}, "t=0x04 d=0x4f800000"},
    // Past FLT_MAX.
    {{"dimen", "1e39"}, "t=0x04 d=0x7f800000"}, {{"dimen", "-1e39"}, "t=0x04 d=0xff800000"},
    {{"dimen", "1e400"}, "t=0x04 d=0x7f800000"}, {{"dimen", "3.4028236e38"}, "t=0x04 d=0x7f800000"},
    {{"dimen", "3.4028235677973367e38"}, "t=0x04 d=0x7f800000"},
    {{"dimen", "1e39dp"}, "t=0x05 d=0x00000001"}, {{"dimen", "-1e39dp"}, "t=0x05 d=0x00000001"},
    {{"dimen", "1e39%"}, "t=0x06 d=0x00000000"}, {{"fraction", "1e39%"}, "t=0x06 d=0x00000000"},
    {{"fraction", "-1e39%"}, "t=0x06 d=0x00000000"}, {{"any", "-1e39dp"}, "t=0x05 d=0x00000001"},
    // Dimensions of 2^41 and up, undefined in aapt2.
    {{"dimen", "3e12dp"}, "t=0x05 d=0x00000001"}, {{"dimen", "-3e12dp"}, "t=0x05 d=0x00000001"},
    {{"dimen", "2199023255552dp"}, "t=0x05 d=0x00000001"},
    {{"dimen", "1e20px"}, "t=0x05 d=0x00000000"},
};
// clang-format on

TEST(Aapt2Value, ParsesLikeAapt2) {
  ASSERT_GT(rows().size(), 4000u);
  std::set<std::pair<std::string, std::string>> deliberate;
  for (const Row& row : rows()) {
    const uint32_t formats = formatsOf(row.tag);
    ASSERT_NE(formats, 0u) << "unknown tag " << row.tag;
    const std::string where = row.tag + " \"" + row.text + '"';
    const std::optional<Value> v = parseLiteral(row.text, formats);
    const std::optional<ResValue> hydroid =
        v ? std::optional<ResValue>(toResValue(*v)) : std::nullopt;

    if (row.text.empty()) {
      // aapt2 makes an empty element @null; that is the values parser's job (step 4).
      const ResValue null(kTypeReference, 0);
      EXPECT_TRUE(!row.aapt2 || *row.aapt2 == null) << where;
      EXPECT_FALSE(hydroid) << where;
      continue;
    }
    const auto listed = kDeliberateRejects.find({row.tag, row.text});
    if (row.aapt2 && !hydroid && listed != kDeliberateRejects.end()) {
      deliberate.insert({row.tag, row.text});
      EXPECT_EQ(describe(row.aapt2), listed->second) << where << ": aapt2 writes another now";
      continue;
    }
    EXPECT_EQ(describe(hydroid), describe(row.aapt2)) << where;
  }
  // Each listed text is in the golden file, aapt2 took it, and hydroid did not.
  for (const auto& entry : kDeliberateRejects) {
    const auto& [tag, text] = entry.first;
    EXPECT_TRUE(deliberate.count(entry.first))
        << tag << " \"" << text << "\" is no longer a deliberate rejection";
  }
}

TEST(Aapt2Value, CoversEveryTagAndOutcome) {
  std::map<std::string, std::set<int>> seen;  // tag -> types aapt2 wrote, -1 for none
  for (const Row& row : rows()) seen[row.tag].insert(row.aapt2 ? row.aapt2->first : -1);
  const std::map<std::string, std::set<int>> expected = {
      {"color", {-1, kTypeReference, 0x1c, 0x1d, 0x1e, 0x1f}},
      {"bool", {-1, kTypeReference, kTypeIntBoolean}},
      {"integer", {-1, kTypeReference, kTypeIntDec, kTypeIntHex}},
      {"dimen", {-1, kTypeReference, kTypeFloat, kTypeDimension, kTypeFraction}},
      {"fraction", {kTypeFloat, kTypeDimension, kTypeFraction}},
      {"any",
       {-1, kTypeFloat, kTypeDimension, kTypeFraction, kTypeIntDec, kTypeIntHex, kTypeIntBoolean,
        0x1c, 0x1d, 0x1e, 0x1f}},
  };
  EXPECT_EQ(seen, expected);
}

}  // namespace
}  // namespace hydroid::res

// core/PathUtf8.h, the UTF-8 of a path that never throws, and how a load takes a Windows name that
// is not Unicode text (a lone surrogate), as aapt2 37.0.0 does (#111): such a folder of res/ has
// nothing in it, such a file or folder in a type folder is not found, and a diagnostic writes
// either with U+FFFD.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "TempDir.h"
#include "core/PathUtf8.h"
#include "hydroid/res/ResourceTable.h"

namespace hydroid::res {
namespace {

namespace fs = std::filesystem;

TEST(PathUtf8, IsTheBytesOfAUnicodePath) {
  EXPECT_EQ(detail::utf8(fs::u8path("res/values-ko/\xea\xb0\x80.xml")),
            "res/values-ko/\xea\xb0\x80.xml");
  EXPECT_EQ(detail::utf8(fs::u8path("\xf0\x9f\x98\x80")), "\xf0\x9f\x98\x80");  // a pair in UTF-16
  EXPECT_TRUE(detail::isUnicode(fs::u8path("\xf0\x9f\x98\x80")));
}

#ifdef _WIN32
TEST(PathUtf8, WritesALoneSurrogateAsReplacement) {
  const std::string kReplacement = "\xef\xbf\xbd";
  const std::vector<std::pair<std::wstring, std::string>> cases = {
      {L"a\xd800"
       L"b",
       "a" + kReplacement + "b"},                      // a high surrogate alone
      {L"a\xdc00", "a" + kReplacement},                // a low one alone
      {L"\xd800", kReplacement},                       // at the end
      {L"\xdc00\xd800", kReplacement + kReplacement},  // a pair the wrong way round
  };
  for (const auto& [name, expected] : cases) {
    EXPECT_EQ(detail::utf8(fs::path(name)), expected);
    EXPECT_FALSE(detail::isUnicode(fs::path(name)));
  }
}

// Writes `text` to `path`, a wide path that UTF-8 cannot name.
void write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string shown(const Diagnostic& d) {
  std::ostringstream out;
  out << d;
  return out.str();
}

TEST(PathUtf8, FileNotUnicodeIsNotFound) {
  test::TempDir dir;
  const fs::path res = dir.path() / "res";
  write(res / "values" / L"b\xd800.xml", "<resources><string name='s'>x</string></resources>");
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(res, "com.example", kAppPackageId, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "file not found");
  EXPECT_NE(shown(errors[0]).find("b\xef\xbf\xbd.xml: file not found"), std::string::npos)
      << shown(errors[0]);
}

TEST(PathUtf8, FolderNotUnicodeHasNothingInIt) {
  test::TempDir dir;
  const fs::path res = dir.path() / "res";
  write(res / "values" / "v.xml", "<resources><string name='s'>x</string></resources>");
  write(res / L"values-land\xd800" / "v.xml", "<resources><string name='t'>y</string></resources>");
  ResourceTable table;
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(table.addPackage(res, "com.example", kAppPackageId, &errors));
  EXPECT_TRUE(table.find("com.example:string/s"));
  EXPECT_FALSE(table.find("com.example:string/t"));
}

// In a type folder, aapt2 does not find a folder so named either, though it skips one with a
// Unicode name: raw/sub<U+D800>/x.txt, values/sub<U+D800>/ and an empty raw/e<U+D800>/ each fail.
TEST(PathUtf8, FolderNotUnicodeInATypeFolderIsNotFound) {
  for (const fs::path& sub :
       {fs::path("raw") / L"sub\xd800" / "x.txt", fs::path("values") / L"sub\xd800" / "v.xml",
        fs::path("raw") / L"e\xd800"}) {
    test::TempDir dir;
    const fs::path res = dir.path() / "res";
    if (sub.has_extension()) {
      write(res / sub, "<resources/>");
    } else {
      fs::create_directories(res / sub);
    }
    ResourceTable table;
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(table.addPackage(res, "com.example", kAppPackageId, &errors)) << detail::utf8(sub);
    ASSERT_EQ(errors.size(), 1u) << detail::utf8(sub);
    EXPECT_EQ(errors[0].message, "file not found");
  }
  // A Unicode name, as file-in-subfolder of the golden cases.
  test::TempDir dir;
  write(dir.path() / "res" / "raw" / "sub" / "x.txt", "x");
  ResourceTable table;
  EXPECT_TRUE(table.addPackage(dir.path() / "res", "com.example", kAppPackageId));
}

TEST(PathUtf8, ResFolderNotUnicodeIsWrittenWithReplacement) {
  test::TempDir dir;
  ResourceTable table;
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(table.addPackage(dir.path() / L"res\xd800", "com.example", kAppPackageId, &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_NE(shown(errors[0]).find("res\xef\xbf\xbd: not a folder"), std::string::npos)
      << shown(errors[0]);
}
#endif

}  // namespace
}  // namespace hydroid::res

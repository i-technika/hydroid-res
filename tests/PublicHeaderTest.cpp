// Public headers are ASCII only (#15). This project builds with /utf-8, but a project that
// includes them may not: MSVC then reads them in the system code page, warns C4819 on CP949 for
// bytes it can't map, and /WX turns that into a build failure.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace hydroid::res {
namespace {

namespace fs = std::filesystem;

TEST(PublicHeaders, AreAscii) {
  int files = 0;
  for (const auto& entry : fs::recursive_directory_iterator(HYDROID_INCLUDE_DIR)) {
    if (!entry.is_regular_file()) continue;
    ++files;
    const std::string name = fs::relative(entry.path(), HYDROID_INCLUDE_DIR).generic_string();
    std::ifstream in(entry.path(), std::ios::binary);
    std::string line;
    for (int n = 1; std::getline(in, line); ++n) {
      auto bad = std::find_if(line.begin(), line.end(),
                              [](unsigned char c) { return c >= 0x80; });
      if (bad != line.end()) {
        ADD_FAILURE() << name << ":" << n << ":" << (bad - line.begin() + 1)
                      << ": non-ASCII byte in a public header";
      }
    }
  }
  EXPECT_GT(files, 0) << "no files under " << HYDROID_INCLUDE_DIR;
}

}  // namespace
}  // namespace hydroid::res

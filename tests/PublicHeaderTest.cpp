// Public headers are ASCII only (#15). This project builds with /utf-8, but a project that
// includes them may not: MSVC then reads them in the system code page, warns C4819 on CP949 for
// bytes it can't map, and /WX turns that into a build failure.
//
// Every directory hydroid::res puts on its consumers' include path is checked, so the headers
// generated in the build tree are too (#13).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace hydroid::res {
namespace {

namespace fs = std::filesystem;

TEST(PublicHeaders, AreAscii) {
  // hydroid-res's INTERFACE_INCLUDE_DIRECTORIES, joined with '|' (tests/CMakeLists.txt).
  std::istringstream dirs(HYDROID_PUBLIC_INCLUDE_DIRS);
  std::string dir;
  int dirCount = 0;
  while (std::getline(dirs, dir, '|')) {
    ++dirCount;
    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;
      ++files;
      const std::string name = entry.path().generic_string();
      std::ifstream in(entry.path(), std::ios::binary);
      std::string line;
      for (int n = 1; std::getline(in, line); ++n) {
        auto bad =
            std::find_if(line.begin(), line.end(), [](unsigned char c) { return c >= 0x80; });
        if (bad != line.end()) {
          ADD_FAILURE() << name << ":" << n << ":" << (bad - line.begin() + 1)
                        << ": non-ASCII byte in a public header";
        }
      }
    }
    EXPECT_GT(files, 0) << "no files under " << dir;
  }
  // The source include/ and the generated one.
  EXPECT_GE(dirCount, 2) << "include directories: " << HYDROID_PUBLIC_INCLUDE_DIRS;
}

}  // namespace
}  // namespace hydroid::res

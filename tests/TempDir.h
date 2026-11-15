// A folder of its own under the system's temporary folder, removed with what is in it at the end
// of its scope, for tests that load res/ folders they write.

#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace hydroid::res::test {

class TempDir {
 public:
  TempDir() {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("hydroid-res-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const { return path_; }

  // Writes `content` to `relative`, a UTF-8 path with '/', making the folders it needs.
  void write(std::string_view relative, std::string_view content) const {
    const std::filesystem::path file = path_ / std::filesystem::u8path(relative);
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    ASSERT_TRUE(out) << "cannot write " << relative;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace hydroid::res::test

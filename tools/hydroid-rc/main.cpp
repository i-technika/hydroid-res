// hydroid-rc: compiles an Android res/ folder into a .hrt binary table.
// Compilation lands in step 9; for now it prints a res/ folder as `aapt2 dump resources` does.

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Version.h"

namespace {

namespace fs = std::filesystem;

int usage() {
  std::fprintf(stderr,
               "usage: hydroid-rc --version\n"
               "       hydroid-rc dump <res-folder> [<package>]\n"
               "  dump  loads the folder as <package> (com.example by default), id 7f, and prints\n"
               "        it as `aapt2 dump resources` prints an APK\n");
  return 2;
}

// The UTF-8 text of an argument, whether u8string() returns std::string (C++17) or std::u8string.
std::string utf8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

int dump(const fs::path& res, const std::string& package) {
  hydroid::res::ResourceTable table;
  std::vector<hydroid::res::Diagnostic> errors;
  if (!table.addPackage(res, package, hydroid::res::kAppPackageId, &errors)) {
    for (const hydroid::res::Diagnostic& e : errors) {
      // Whole lines, so that a path with no UTF-8 for it fails before any of its line is out.
      std::ostringstream line;
      line << "error: " << e << '\n';
      std::cerr << line.str();
    }
    return 1;
  }
  hydroid::res::dump(std::cout, table);
  return 0;
}

// Takes the arguments as paths, so that the folder reaches addPackage whole and the package as
// UTF-8: on Windows, the narrow argv of main holds them in the ANSI code page, which writes ? for
// a character it lacks, so wmain takes them in UTF-16 instead (#87). UTF-16 that is not Unicode
// text, as a lone surrogate is, has no UTF-8: u8string() throws, and that is an error, not an
// abort.
int run(const std::vector<fs::path>& args) {
  if (args.size() == 2 && args[1] == "--version") {
    std::printf("hydroid-rc %s\n", hydroid::res::version());
    return 0;
  }
  if ((args.size() == 3 || args.size() == 4) && args[1] == "dump") {
    try {
      return dump(args[2], args.size() == 4 ? utf8(args[3]) : "com.example");
    } catch (const std::system_error& e) {
      std::cerr << "error: an argument is not Unicode text (" << e.what() << ")\n";
      return 1;
    }
  }
  return usage();
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#else
int main(int argc, char** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#endif

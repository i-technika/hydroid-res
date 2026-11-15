// hydroid-rc: compiles an Android res/ folder into a .hrt binary table.
// Compilation lands in step 9; for now it prints a res/ folder as `aapt2 dump resources` does.

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Version.h"

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: hydroid-rc --version\n"
               "       hydroid-rc dump <res-folder> [<package>]\n"
               "  dump  loads the folder as <package> (com.example by default), id 7f, and prints\n"
               "        it as `aapt2 dump resources` prints an APK\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("hydroid-rc %s\n", hydroid::res::version());
    return 0;
  }
  if ((argc == 3 || argc == 4) && std::strcmp(argv[1], "dump") == 0) {
    hydroid::res::ResourceTable table;
    std::vector<hydroid::res::Diagnostic> errors;
    if (!table.addPackage(argv[2], argc == 4 ? argv[3] : "com.example", hydroid::res::kAppPackageId,
                          &errors)) {
      for (const hydroid::res::Diagnostic& e : errors) std::cerr << "error: " << e << '\n';
      return 1;
    }
    hydroid::res::dump(std::cout, table);
    return 0;
  }
  return usage();
}

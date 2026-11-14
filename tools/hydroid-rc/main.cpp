// hydroid-rc: compiles an Android res/ folder into a .hrt binary table.
// Compilation lands in step 8; for now only --version works.

#include <cstdio>
#include <cstring>

#include "hydroid/res/Version.h"

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("hydroid-rc %s\n", hydroid::res::version());
    return 0;
  }
  std::fprintf(stderr, "usage: hydroid-rc --version\n");
  return 2;
}

// hydroid-rc: compiles an Android res/ folder into a .hrt binary table.
// Compilation lands in step 9; for now it prints a res/ folder as `aapt2 dump resources` does,
// or the values of it that a device picks.

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"
#include "hydroid/res/Version.h"

namespace {

namespace fs = std::filesystem;
namespace res = hydroid::res;

int usage() {
  std::fprintf(stderr,
               "usage: hydroid-rc --version\n"
               "       hydroid-rc dump <res-folder> [<package>]\n"
               "       hydroid-rc pick <res-folder> <qualifiers> [--locales <tag,...>] "
               "[<package>]\n"
               "  dump  loads the folder as <package> (com.example by default), id 7f, and prints\n"
               "        it as `aapt2 dump resources` prints an APK\n"
               "  pick  loads it so too, and prints the value each resource has on the device\n"
               "        that <qualifiers> describe as a folder name does (ko-rKR-land-xhdpi; \"\"\n"
               "        for none), and whose languages, the most wanted first, are the BCP 47\n"
               "        tags of --locales (ko-KR,en-US): first the locale it reads them in, then\n"
               "        a line for each resource, with the configuration of its value. Without a\n"
               "        version (v34) among the qualifiers, the device is of API %d\n",
               res::kEmulatedSdkVersion);
  return 2;
}

// The UTF-8 text of an argument, whether u8string() returns std::string (C++17) or std::u8string.
std::string utf8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

std::optional<res::ResourceTable> load(const fs::path& folder, const std::string& package) {
  res::ResourceTable table;
  std::vector<res::Diagnostic> errors;
  if (!table.addPackage(folder, package, res::kAppPackageId, &errors)) {
    for (const res::Diagnostic& e : errors) std::cerr << "error: " << e << '\n';
    return std::nullopt;
  }
  return table;
}

int dump(const fs::path& folder, const std::string& package) {
  const std::optional<res::ResourceTable> table = load(folder, package);
  if (!table) return 1;
  res::dump(std::cout, *table);
  return 0;
}

// Whether one of the '-' parts of a folder name is a version, as v21 or v36.1 are.
bool namesVersion(std::string_view qualifiers) {
  while (true) {
    const size_t dash = qualifiers.find('-');
    const std::string_view part = qualifiers.substr(0, dash);
    if (part.size() > 1 && (part[0] == 'v' || part[0] == 'V') && part[1] >= '0' && part[1] <= '9') {
      return true;
    }
    if (dash == std::string_view::npos) return false;
    qualifiers.remove_prefix(dash + 1);
  }
}

// The device of `qualifiers`, as parseConfig reads a folder name, but for the version that it
// raises to the first that knows a qualifier (night is night-v8): a device is of the version it
// says, and of kEmulatedSdkVersion, through setConfiguration, if it says none.
std::optional<res::Config> deviceOf(const std::string& qualifiers) {
  std::optional<res::Config> device = res::parseConfig(qualifiers);
  if (device && !namesVersion(qualifiers)) {
    device->sdkVersion = 0;
    device->minorVersion = 0;
  }
  return device;
}

// "ko-KR,en-US" as the locales of setConfiguration.
std::optional<std::vector<res::Config>> localesOf(std::string_view tags) {
  std::vector<res::Config> locales;
  while (true) {
    const size_t comma = tags.find(',');
    const std::optional<res::Config> locale = res::parseLanguageTag(tags.substr(0, comma));
    if (!locale) return std::nullopt;
    locales.push_back(*locale);
    if (comma == std::string_view::npos) return locales;
    tags.remove_prefix(comma + 1);
  }
}

// What the device of `device` and `locales` picks of the table: the locale it chose, as
// "locale (ko-rKR)", then for each resource of the folder its line of `dump`, followed by the
// configuration and the value it picked, or by "no value" if none matches. The lines that follow
// a style, array, plurals or attribute are indented under it.
int pick(const fs::path& folder, const std::string& package, const res::Config& device,
         const std::vector<res::Config>& locales) {
  const std::optional<res::ResourceTable> table = load(folder, package);
  if (!table) return 1;
  res::Resources resources(*table);
  resources.setConfiguration(device, locales);
  const res::Config configuration = resources.configuration();
  res::Config locale;
  locale.lang = configuration.lang;
  locale.region = configuration.region;
  locale.script = configuration.script;
  locale.variant = configuration.variant;
  std::cout << "locale (" << res::toString(locale) << ")\n";
  for (const res::Package& p : table->packages()) {
    for (const res::TypeTable& type : p.types) {
      if (type.type == res::ResType::Styleable) continue;  // left out of dump too
      for (const res::Entry& entry : type.entries) {
        const res::ResId id = res::makeResId(p.id, type.id, entry.id);
        char hex[16];
        std::snprintf(hex, sizeof hex, "0x%08x", static_cast<unsigned>(id));
        std::cout << "resource " << hex << ' ' << res::toString(type.type) << '/' << entry.name;
        const res::Candidate* candidate = resources.candidate(id);
        if (candidate == nullptr) {
          std::cout << " no value\n";
          continue;
        }
        std::cout << " (" << res::toString(candidate->config) << ") ";
        for (const char c : res::describe(p, type.type, candidate->value)) {
          std::cout << c;
          if (c == '\n') std::cout << "  ";
        }
        std::cout << '\n';
      }
    }
  }
  return 0;
}

// Takes the arguments as paths, so that the folder reaches addPackage whole and the package as
// UTF-8: on Windows, the narrow argv of main holds them in the ANSI code page, which writes ? for
// a character it lacks, so wmain takes them in UTF-16 instead (#87). UTF-16 that is not Unicode
// text, as a lone surrogate is, has no UTF-8: u8string() throws on it, and for the package that is
// an error, not an abort. The library writes such a path with U+FFFD for what it lacks (#111).
int run(const std::vector<fs::path>& args) {
  if (args.size() == 2 && args[1] == "--version") {
    std::printf("hydroid-rc %s\n", res::version());
    return 0;
  }
  if ((args.size() == 3 || args.size() == 4) && args[1] == "dump") {
    std::string package = "com.example";
    if (args.size() == 4) {
      try {
        package = utf8(args[3]);
      } catch (const std::system_error&) {
        std::cerr << "error: the package name is not Unicode text\n";
        return 1;
      }
    }
    // addPackage throws nothing about the folder, which it reports among its errors (#85).
    return dump(args[2], package);
  }
  if (args.size() >= 4 && args[1] == "pick") {
    // The qualifiers, then --locales and the package in either order.
    std::vector<std::string> rest;
    try {
      for (size_t i = 3; i < args.size(); i++) rest.push_back(utf8(args[i]));
    } catch (const std::system_error&) {
      std::cerr << "error: an argument is not Unicode text\n";
      return 1;
    }
    std::optional<std::string> tags;
    std::optional<std::string> package;
    for (size_t i = 1; i < rest.size(); i++) {
      if (rest[i] == "--locales" && !tags && i + 1 < rest.size()) {
        tags = rest[++i];
      } else if (rest[i] != "--locales" && !package) {
        package = rest[i];
      } else {
        return usage();
      }
    }
    const std::optional<res::Config> device = deviceOf(rest[0]);
    if (!device) {
      std::cerr << "error: not the qualifiers of a folder name: " << rest[0] << '\n';
      return 1;
    }
    std::vector<res::Config> locales;
    if (tags) {
      std::optional<std::vector<res::Config>> parsed = localesOf(*tags);
      if (!parsed) {
        std::cerr << "error: not a list of BCP 47 language tags: " << *tags << '\n';
        return 1;
      }
      locales = std::move(*parsed);
    }
    return pick(args[2], package.value_or("com.example"), *device, locales);
  }
  return usage();
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#else
int main(int argc, char** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#endif

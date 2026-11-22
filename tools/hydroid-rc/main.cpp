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
               "       hydroid-rc pick <res-folder> <qualifiers> [--locales <tag,...>]\n"
               "                       [--locale-config <file>]\n"
               "                       [--then <qualifiers> [--locales <tag,...>]]... [<package>]\n"
               "  dump  loads the folder as <package> (com.example by default), id 7f, and prints\n"
               "        it as `aapt2 dump resources` prints an APK\n"
               "  pick  loads it so too, and prints the value each resource has on the device\n"
               "        that <qualifiers> describe as a folder name does (ko-rKR-land-xhdpi; \"\"\n"
               "        for none), and whose languages, the most wanted first, are the BCP 47\n"
               "        tags of --locales (ko-KR,en-US): first the locale it reads them in, then\n"
               "        a line for each resource, with the configuration of its value. Without a\n"
               "        version (v34) among the qualifiers, the device is of API %d; with one,\n"
               "        of that one, even where a folder so named is of a later (night-v4)\n"
               "    --locale-config  the app's LocaleConfig (res/xml/locales_config.xml), which\n"
               "        the resources are given after the first device, as Android gives it, if\n"
               "        that has more than one language: with a default locale, the devices\n"
               "        after it may read them in more than one\n"
               "    --then  a device the same resources are then set to, of the languages of\n"
               "        the one before unless --locales follows: pick prints the lines of each\n"
               "        device in turn, each starting with its locale\n",
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

// The '-' part of a folder name that is its version, as v21 or v36.1 are, or "" for none.
std::string_view versionPart(std::string_view qualifiers) {
  while (true) {
    const size_t dash = qualifiers.find('-');
    const std::string_view part = qualifiers.substr(0, dash);
    if (part.size() > 1 && (part[0] == 'v' || part[0] == 'V') && part[1] >= '0' && part[1] <= '9') {
      return part;
    }
    if (dash == std::string_view::npos) return {};
    qualifiers.remove_prefix(dash + 1);
  }
}

// The device of `qualifiers`, as parseConfig reads a folder name, but for the version that it
// raises to the first that knows a qualifier (night is night-v8, and night-v4 too): a device is
// of the version it says, and of kEmulatedSdkVersion, through setConfiguration, if it says none.
// nullopt, said on stderr, for what is no folder name, and for a version no device has, which a
// folder may name only as it is raised: anydpi-v4.1 is anydpi-v21, but a minor version needs v36
// or later.
std::optional<res::Config> deviceOf(const std::string& qualifiers) {
  std::optional<res::Config> device = res::parseConfig(qualifiers);
  if (!device) {
    std::cerr << "error: not the qualifiers of a folder name: " << qualifiers << '\n';
    return std::nullopt;
  }
  std::optional<res::Config> version = res::Config();  // version 0, for none
  const std::string_view part = versionPart(qualifiers);
  if (!part.empty()) version = res::parseConfig(part);
  if (!version) {
    std::cerr << "error: not the version of a device: " << part
              << " (a minor version needs v36 or later)\n";
    return std::nullopt;
  }
  device->sdkVersion = version->sdkVersion;
  device->minorVersion = version->minorVersion;
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

// A device as setConfiguration takes it.
struct Device {
  res::Config config;
  std::vector<res::Config> locales;
};

// Prints what `resources` pick of `table`: the locale they chose, as "locale (ko-rKR)", then for
// each resource of the folder its line of `dump`, PUBLIC and all, followed by the configuration
// and the value it picked, or by "no value" if none matches. The lines that follow a style,
// array, plurals or attribute are indented under it.
void print(const res::ResourceTable& table, const res::Resources& resources) {
  const res::Config configuration = resources.configuration();
  res::Config locale;
  locale.lang = configuration.lang;
  locale.region = configuration.region;
  locale.script = configuration.script;
  locale.variant = configuration.variant;
  std::cout << "locale (" << res::toString(locale) << ")\n";
  for (const res::Package& p : table.packages()) {
    for (const res::TypeTable& type : p.types) {
      if (type.type == res::ResType::Styleable) continue;  // left out of dump too
      for (const res::Entry& entry : type.entries) {
        const res::ResId id = res::makeResId(p.id, type.id, entry.id);
        char hex[16];
        std::snprintf(hex, sizeof hex, "0x%08x", static_cast<unsigned>(id));
        std::cout << "resource " << hex << ' ' << res::toString(type.type) << '/' << entry.name
                  << (entry.isPublic ? " PUBLIC" : "");
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
}

// What one Resources of the table picks as it is set to each of `devices` in turn, printed for
// each. As ContextImpl.setResources does, it is given `localeConfig`, if any, after the first,
// and only if that has more than one locale: an app first shown in one language never reads its
// resources in more, whatever the user adds later (golden/localeconfig's "fr then fr+es").
int pick(const fs::path& folder, const std::string& package, const std::vector<Device>& devices,
         const std::optional<res::LocaleConfig>& localeConfig) {
  const std::optional<res::ResourceTable> table = load(folder, package);
  if (!table) return 1;
  res::Resources resources(*table);
  for (size_t i = 0; i < devices.size(); i++) {
    resources.setConfiguration(devices[i].config, devices[i].locales);
    if (i == 0 && localeConfig && devices[i].locales.size() > 1) {
      resources.setLocaleConfig(*localeConfig);
    }
    print(*table, resources);
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
    // The qualifiers, then --locales, --locale-config and the package in any order, then each
    // --then with its qualifiers and the --locales of those. The file of --locale-config stays a
    // path, as the folder does.
    struct Step {
      std::string qualifiers;
      std::optional<std::string> tags;
    };
    std::vector<Step> steps;
    std::optional<fs::path> localeConfigFile;
    std::optional<std::string> package;
    try {
      steps.push_back({utf8(args[3]), std::nullopt});
      for (size_t i = 4; i < args.size(); i++) {
        const bool last = i + 1 == args.size();
        if (args[i] == "--locale-config") {
          if (localeConfigFile || last) return usage();
          localeConfigFile = args[++i];
          continue;
        }
        const std::string arg = utf8(args[i]);
        if (arg == "--locales" && !steps.back().tags && !last) {
          steps.back().tags = utf8(args[++i]);
        } else if (arg == "--then" && !last) {
          steps.push_back({utf8(args[++i]), std::nullopt});
        } else if (arg != "--locales" && arg != "--then" && !package) {
          package = arg;
        } else {
          return usage();
        }
      }
    } catch (const std::system_error&) {
      std::cerr << "error: an argument is not Unicode text\n";
      return 1;
    }
    std::vector<Device> devices;
    for (const Step& step : steps) {
      const std::optional<res::Config> device = deviceOf(step.qualifiers);
      if (!device) return 1;
      // A device of no --locales after another has the languages of that one, as a phone that is
      // turned keeps them.
      std::vector<res::Config> locales;
      if (!devices.empty()) locales = devices.back().locales;
      if (step.tags) {
        std::optional<std::vector<res::Config>> parsed = localesOf(*step.tags);
        if (!parsed) {
          std::cerr << "error: not a list of BCP 47 language tags: " << *step.tags << '\n';
          return 1;
        }
        locales = std::move(*parsed);
      }
      devices.push_back({*device, std::move(locales)});
    }
    std::optional<res::LocaleConfig> localeConfig;
    if (localeConfigFile) {
      std::vector<res::Diagnostic> errors;
      localeConfig = res::parseLocaleConfig(*localeConfigFile, &errors);
      if (!localeConfig) {
        for (const res::Diagnostic& e : errors) std::cerr << "error: " << e << '\n';
        return 1;
      }
    }
    return pick(args[2], package.value_or("com.example"), devices, localeConfig);
  }
  return usage();
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#else
int main(int argc, char** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#endif

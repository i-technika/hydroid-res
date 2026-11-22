// Resources: the configuration it makes of a device's and the user's languages, as Android's
// ResourcesImpl and LocaleList do, the values it picks from a table, and how long it keeps them.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "TempDir.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"
#include "hydroid/res/Version.h"

namespace hydroid::res {
namespace {

using test::TempDir;

Config cfg(const std::string& qualifiers) {
  const std::optional<Config> c = parseConfig(qualifiers);
  EXPECT_TRUE(c) << qualifiers;
  return c.value_or(Config{});
}

// The state of a device of those qualifiers, without the version a folder of them would have.
Config dev(const std::string& qualifiers) {
  Config c = cfg(qualifiers);
  c.sdkVersion = 0;
  return c;
}

std::vector<Config> tags(const std::vector<const char*>& languageTags) {
  std::vector<Config> locales;
  for (const char* tag : languageTags) {
    const std::optional<Config> locale = parseLanguageTag(tag);
    EXPECT_TRUE(locale) << tag;
    locales.push_back(locale.value_or(Config{}));
  }
  return locales;
}

// Adds a package whose string "where" says which folder it was taken from, with a value in
// values/ and in each values-<qualifiers>/ of `folders`.
void addPackage(ResourceTable& table, const TempDir& dir, const std::vector<const char*>& folders,
                const std::string& name = "com.example", uint8_t id = kAppPackageId) {
  const std::filesystem::path res = dir.path() / name;
  const auto write = [&](const std::string& folder, const std::string& text) {
    std::filesystem::create_directories(res / folder);
    std::ofstream(res / folder / "strings.xml", std::ios::binary)
        << "<resources><string name=\"where\">" << text << "</string></resources>";
  };
  write("values", "default");
  for (const char* folder : folders) write(std::string("values-") + folder, folder);
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(table.addPackage(res, name, id, &errors))
      << (errors.empty() ? "" : errors[0].message);
}

// The folder Resources takes "where" from.
std::string where(const Resources& resources, const ResourceTable& table,
                  const std::string& package = "com.example") {
  const std::optional<ResId> id = table.find(package, ResType::String, "where");
  if (!id) return "no resource";
  const Candidate* c = resources.candidate(*id);
  if (c == nullptr) return "none";
  return table.package(package)->strings.at(c->value.index());
}

// Whether `candidate` is one of the values `table` holds now for `id`, and not one of a table that
// is gone, which may read as one of these when its memory has not been taken again.
bool isOfTheTable(const Candidate* candidate, const ResourceTable& table, ResId id) {
  const Entry* entry = table.entry(id);
  if (entry == nullptr) return false;
  for (const Candidate& c : entry->candidates) {
    if (&c == candidate) return true;
  }
  return false;
}

// The locale Resources chose, as a folder would name it.
std::string chosen(const Resources& resources) {
  Config c = resources.configuration();
  c.sdkVersion = 0;
  return toString(c);
}

TEST(Resources, DefaultConfigurationTakesTheDefaultValues) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko", "night", "v21", "v38"});
  const Resources resources(table);
  EXPECT_EQ(resources.configuration().sdkVersion, kEmulatedSdkVersion);
  EXPECT_EQ(resources.generation(), 0u);
  // Of the default and v21, which both match, the higher version.
  EXPECT_EQ(where(resources, table), "v21");
}

// A density never keeps a value from matching (ResTable_config::match has no test of it), and the
// default configuration asks for none, which isBetterThan takes as medium.
TEST(Resources, DefaultConfigurationTakesValuesOfADensity) {
  const auto pick = [](const std::vector<const char*>& folders, bool withDefault) {
    TempDir dir;
    ResourceTable table;
    const std::filesystem::path res = dir.path() / "res";
    const auto write = [&](const std::string& folder, const std::string& text) {
      std::filesystem::create_directories(res / folder);
      std::ofstream(res / folder / "strings.xml", std::ios::binary)
          << "<resources><string name=\"where\">" << text << "</string></resources>";
    };
    if (withDefault) write("values", "default");
    for (const char* folder : folders) write(std::string("values-") + folder, folder);
    EXPECT_TRUE(table.addPackage(res, "com.example", kAppPackageId));
    const Resources resources(table);
    return where(resources, table);
  };
  // anydpi before all. mdpi is asked for, and no density is mdpi too: values-mdpi/ is as good as
  // values/ and comes after it in the table, so it is taken. And none before another density.
  EXPECT_EQ(pick({"anydpi", "mdpi", "hdpi"}, true), "anydpi");
  EXPECT_EQ(pick({"mdpi"}, true), "mdpi");
  EXPECT_EQ(pick({"mdpi", "hdpi"}, true), "mdpi");
  EXPECT_EQ(pick({"hdpi", "ldpi"}, true), "default");
  // And where there is no value without one, a value of a density all the same.
  for (const char* density : {"ldpi", "hdpi", "xxhdpi", "nodpi"}) {
    SCOPED_TRACE(density);
    EXPECT_EQ(pick({density}, false), density);
  }
  // Not so the qualifiers of the screen, which the default configuration has none of.
  EXPECT_EQ(pick({"sw1dp"}, false), "none");
  EXPECT_EQ(pick({"small"}, false), "none");
}

TEST(Resources, PicksForTheConfiguration) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko", "night", "land", "sw600dp"});
  Resources resources(table);

  resources.setConfiguration(dev("ko-rKR-port-night"));
  EXPECT_EQ(where(resources, table), "ko");
  resources.setConfiguration(dev("en-rUS-port-night"));
  EXPECT_EQ(where(resources, table), "night");
  resources.setConfiguration(dev("en-rUS-sw720dp-land-night"));
  EXPECT_EQ(where(resources, table), "sw600dp");
  resources.setConfiguration(dev("en-rUS-port"));
  EXPECT_EQ(where(resources, table), "default");
}

TEST(Resources, NoCandidateForAnUnknownId) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {});
  const Resources resources(table);
  EXPECT_EQ(resources.candidate(0), nullptr);
  EXPECT_EQ(resources.candidate(makeResId(0x7f, 0x7f, 0)), nullptr);
  EXPECT_EQ(resources.candidate(makeResId(0x01, 1, 0)), nullptr);
}

TEST(Resources, NoCandidateWhereNoValueMatches) {
  TempDir dir;
  ResourceTable table;
  const std::filesystem::path res = dir.path() / "res";
  std::filesystem::create_directories(res / "values-land");
  std::ofstream(res / "values-land" / "strings.xml")
      << "<resources><string name=\"where\">land</string></resources>";
  ASSERT_TRUE(table.addPackage(res, "com.example", kAppPackageId));
  Resources resources(table);
  resources.setConfiguration(dev("port"));
  EXPECT_EQ(where(resources, table), "none");
  resources.setConfiguration(dev("land"));
  EXPECT_EQ(where(resources, table), "land");
}

TEST(Resources, VersionOfTheDevice) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"v21", "v37", "v38"});
  Resources resources(table);

  // A device that gives none is kEmulatedSdkVersion.
  resources.setConfiguration(dev("port"));
  EXPECT_EQ(resources.configuration().sdkVersion, kEmulatedSdkVersion);
  EXPECT_EQ(where(resources, table), "v37");
  // One that gives its own keeps it.
  resources.setConfiguration(cfg("port-v30"));
  EXPECT_EQ(resources.configuration().sdkVersion, 30);
  EXPECT_EQ(where(resources, table), "v21");
  resources.setConfiguration(cfg("v38"));
  EXPECT_EQ(where(resources, table), "v38");
  // A minor version alone is a version all the same, and kept.
  Config minorOnly = dev("port");
  minorOnly.minorVersion = 1;
  resources.setConfiguration(minorOnly);
  EXPECT_EQ(resources.configuration().sdkVersion, 0);
  EXPECT_EQ(resources.configuration().minorVersion, 1);
  EXPECT_EQ(where(resources, table), "default");
}

TEST(Resources, TheLongerSideOfTheScreenIsItsWidth) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"800x480", "2000x1000"});
  Resources resources(table);

  // A phone held upright: as it is, 1080 wide is less than 2000 and only 800x480 matches; as
  // 2400x1080, both do, and 2000x1000 is the closer.
  Config upright;
  upright.screenWidth = 1080;
  upright.screenHeight = 2400;
  resources.setConfiguration(upright);
  EXPECT_EQ(resources.configuration().screenWidth, 2400);
  EXPECT_EQ(resources.configuration().screenHeight, 1080);
  EXPECT_EQ(where(resources, table), "2000x1000");

  // Turned on its side it is the same configuration, and nothing changes.
  const uint32_t generation = resources.generation();
  Config onItsSide;
  onItsSide.screenWidth = 2400;
  onItsSide.screenHeight = 1080;
  resources.setConfiguration(onItsSide);
  EXPECT_EQ(resources.generation(), generation);
  EXPECT_EQ(where(resources, table), "2000x1000");
}

TEST(Resources, NavHiddenIsNotTheDevices) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"navexposed", "navhidden"});
  Resources resources(table);
  for (const NavHidden navHidden : {NavHidden::No, NavHidden::Yes}) {
    Config device;
    device.navHidden = navHidden;
    resources.setConfiguration(device);
    EXPECT_EQ(resources.configuration().navHidden, NavHidden::Any);
    EXPECT_EQ(where(resources, table), "default");
  }
}

TEST(Resources, GenerationChangesWithTheConfiguration) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  const uint32_t first = resources.generation();

  resources.setConfiguration(dev("night"));
  const uint32_t night = resources.generation();
  EXPECT_NE(night, first);
  EXPECT_EQ(where(resources, table), "night");

  // The same configuration again changes nothing, however it is given.
  resources.setConfiguration(dev("night"));
  EXPECT_EQ(resources.generation(), night);
  resources.setConfiguration(cfg("night-v37"));
  EXPECT_EQ(resources.generation(), night);

  resources.setConfiguration(dev("notnight"));
  EXPECT_NE(resources.generation(), night);
  EXPECT_EQ(where(resources, table), "default");
}

TEST(Resources, KeepsACandidateUntilTheConfigurationChanges) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  const ResId id = *table.find("com.example", ResType::String, "where");

  resources.setConfiguration(dev("night"));
  const Candidate* night = resources.candidate(id);
  ASSERT_NE(night, nullptr);
  EXPECT_EQ(resources.candidate(id), night);
  EXPECT_EQ(toString(night->config), "night-v8");

  resources.setConfiguration(dev("notnight"));
  const Candidate* day = resources.candidate(id);
  ASSERT_NE(day, nullptr);
  EXPECT_NE(day, night);
  // The candidate is the table's: the one picked before is what it was.
  EXPECT_EQ(toString(night->config), "night-v8");
  EXPECT_EQ(toString(day->config), "");
}

TEST(Resources, GivesTheGenerationOfACandidate) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  const ResId id = *table.find("com.example", ResType::String, "where");

  resources.setConfiguration(dev("night"));
  uint32_t generation = 0;
  const Candidate* night = resources.candidate(id, &generation);
  ASSERT_NE(night, nullptr);
  EXPECT_EQ(generation, resources.generation());

  resources.setConfiguration(dev("notnight"));
  uint32_t after = generation;
  EXPECT_NE(resources.candidate(id, &after), night);
  EXPECT_NE(after, generation);
  EXPECT_EQ(after, resources.generation());

  // Of a resource it has not, too: that there is no value is as much of the generation.
  uint32_t none = generation;
  EXPECT_EQ(resources.candidate(makeResId(0x7f, 0x7f, 0), &none), nullptr);
  EXPECT_EQ(none, after);

  // And of the table as it is: one that has added a package since gives a value of it with the
  // generation that came of that, and not the one before.
  addPackage(table, dir, {"ko"}, "com.example.lib", 0x7e);
  uint32_t ofTheTable = after;
  EXPECT_TRUE(isOfTheTable(resources.candidate(id, &ofTheTable), table, id));
  EXPECT_NE(ofTheTable, after);
  EXPECT_EQ(ofTheTable, resources.generation());
}

// The generation a candidate is given with is the one it was picked in (#207), as another thread
// sets the configuration: one asked for after it might be of the configuration set in between.
TEST(Resources, GivesTheGenerationOfACandidateAsTheConfigurationChanges) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  const ResId id = *table.find("com.example", ResType::String, "where");

  // Each setConfiguration changes the configuration, so the generations of night are the odd
  // ones, and those of the default configuration and of notnight the even ones.
  const Config night = dev("night");
  const Config notnight = dev("notnight");
  // The configuration is set, from when all the readers are up, until they have read a good many
  // times as it changes, however the threads are given their turns.
  constexpr int kReaders = 3;
  constexpr int kReads = 1000;
  std::atomic<int> started{0};
  std::atomic<int> reads{0};
  std::atomic<int> wrong{0};
  std::atomic<bool> done{false};
  uint32_t sets = 0;
  std::thread setter([&] {
    while (started < kReaders) std::this_thread::yield();
    for (; sets < 2000 || reads < kReads; sets++) {
      resources.setConfiguration(sets % 2 == 0 ? night : notnight);
    }
    done = true;
  });
  std::vector<std::thread> readers;
  for (int t = 0; t < kReaders; t++) {
    readers.emplace_back([&] {
      started++;
      while (!done) {
        uint32_t generation = 0;
        const Candidate* c = resources.candidate(id, &generation);
        const bool ofNight = c != nullptr && toString(c->config) == "night-v8";
        if (ofNight != (generation % 2 == 1)) wrong++;
        reads++;
      }
    });
  }
  setter.join();
  for (std::thread& reader : readers) reader.join();
  EXPECT_EQ(wrong.load(), 0);
  EXPECT_GE(reads.load(), kReads);
  EXPECT_EQ(resources.generation(), sets);
}

TEST(Resources, SeesAPackageTheTableAdds) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  resources.setConfiguration(dev("night"), tags({"fr-FR", "ko-KR"}));
  EXPECT_EQ(where(resources, table), "night");
  EXPECT_EQ(chosen(resources), "fr-rFR-night");
  const uint32_t before = resources.generation();

  // The table moves its packages as it adds one: what was picked is picked again, and the
  // locale chosen again, now that the table has Korean.
  addPackage(table, dir, {"ko"}, "com.example.lib", 0x7e);
  EXPECT_NE(resources.generation(), before);
  EXPECT_EQ(where(resources, table), "night");
  EXPECT_EQ(where(resources, table, "com.example.lib"), "ko");
  EXPECT_EQ(chosen(resources), "ko-rKR-night");
}

// A table assigned another, even of as many packages, is new to it (#205): nothing picked from the
// one it held is given, and the locale is chosen again.
TEST(Resources, SeesATableAssignedToIt) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  resources.setConfiguration(dev("night"), tags({"fr-FR", "ko-KR"}));
  EXPECT_EQ(where(resources, table), "night");
  EXPECT_EQ(chosen(resources), "fr-rFR-night");
  uint32_t before = resources.generation();

  // The same package and resource, as res/ is read again: the one picked before was of the
  // table that is gone.
  {
    const TempDir otherDir;
    ResourceTable other;
    addPackage(other, otherDir, {"ko"});
    table = std::move(other);
  }
  const ResId id = *table.find("com.example", ResType::String, "where");
  EXPECT_NE(resources.generation(), before);
  EXPECT_EQ(chosen(resources), "ko-rKR-night");
  EXPECT_TRUE(isOfTheTable(resources.candidate(id), table, id));
  EXPECT_EQ(where(resources, table), "ko");

  // Assigned a copy of what it holds, it may hold the values where they were, each assigned its
  // copy, but has changed all the same: only the generation tells it here.
  before = resources.generation();
  const ResourceTable copy = table;
  table = copy;
  EXPECT_NE(resources.generation(), before);
  EXPECT_EQ(where(resources, table), "ko");
}

TEST(Resources, MovesWithWhatItHolds) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources first(table);
  first.setConfiguration(dev("night"));
  Resources second(std::move(first));
  EXPECT_EQ(where(second, table), "night");
  Resources third(table);
  third = std::move(second);
  EXPECT_EQ(where(third, table), "night");
  EXPECT_EQ(third.configuration().uiModeNight, UiModeNight::Yes);
}

TEST(Resources, AnswersFromSeveralThreads) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night", "land", "ko"});
  Resources resources(table);
  const ResId id = *table.find("com.example", ResType::String, "where");

  // gtest's assertions are not for several threads everywhere, so the threads only count.
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; t++) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 200; i++) {
        if (t == 0) {
          resources.setConfiguration(dev(i % 2 == 0 ? "night" : "land"));
          continue;
        }
        const Candidate* c = resources.candidate(id);
        const std::string from = c != nullptr ? toString(c->config) : "none";
        if (from != "" && from != "night-v8" && from != "land") wrong++;
        const Config config = resources.configuration();
        if (config.sdkVersion != kEmulatedSdkVersion) wrong++;
        resources.generation();
      }
    });
  }
  for (std::thread& thread : threads) thread.join();
  EXPECT_EQ(wrong.load(), 0);
}

// ---- another density -------------------------------------------------------------------------

// The folder Resources takes "where" from for the density `density`.
std::string whereFor(const Resources& resources, const ResourceTable& table, uint16_t density) {
  const std::optional<ResId> id = table.find("com.example", ResType::String, "where");
  if (!id) return "no resource";
  const Candidate* c = resources.candidateForDensity(*id, density);
  if (c == nullptr) return "none";
  return table.package("com.example")->strings.at(c->value.index());
}

// As getDrawableForDensity asks for one (#204): the configuration with only the density changed,
// which neither the generation nor what candidate gives know anything of.
TEST(ResourcesDensity, PicksForAnotherDensity) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"hdpi", "xhdpi", "xxhdpi"});
  Resources resources(table);
  resources.setConfiguration(dev("xhdpi"));
  const uint32_t generation = resources.generation();

  EXPECT_EQ(whereFor(resources, table, 0), "xhdpi");
  EXPECT_EQ(whereFor(resources, table, kDensityXhdpi), "xhdpi");
  EXPECT_EQ(whereFor(resources, table, kDensityHdpi), "hdpi");
  EXPECT_EQ(whereFor(resources, table, kDensityXxhdpi), "xxhdpi");
  // Above all of them, the highest; below all, the lowest, which is values/, of mdpi.
  EXPECT_EQ(whereFor(resources, table, kDensityXxxhdpi), "xxhdpi");
  EXPECT_EQ(whereFor(resources, table, kDensityLdpi), "default");

  EXPECT_EQ(resources.configuration().density, kDensityXhdpi);
  EXPECT_EQ(where(resources, table), "xhdpi");
  EXPECT_EQ(resources.generation(), generation);
  uint32_t of = generation + 1;
  resources.candidateForDensity(*table.find("com.example", ResType::String, "where"),
                                kDensityXxhdpi, &of);
  EXPECT_EQ(of, generation);
}

// What is picked for a density is kept apart from what is picked for the device's own, and both
// are thrown away as the configuration or the table change.
TEST(ResourcesDensity, KeepsTheAnswersOfEachDensityUntilTheConfigurationChanges) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"xhdpi", "xxhdpi", "land-xxhdpi"});
  Resources resources(table);
  const ResId id = *table.find("com.example", ResType::String, "where");

  resources.setConfiguration(dev("port-xhdpi"));
  // The device's own first, then another, and the other way round.
  EXPECT_EQ(where(resources, table), "xhdpi");
  EXPECT_EQ(whereFor(resources, table, kDensityXxhdpi), "xxhdpi");
  EXPECT_EQ(whereFor(resources, table, kDensityHdpi), "xhdpi");
  EXPECT_EQ(where(resources, table), "xhdpi");
  const Candidate* xxhdpi = resources.candidateForDensity(id, kDensityXxhdpi);
  EXPECT_EQ(resources.candidateForDensity(id, kDensityXxhdpi), xxhdpi);

  resources.setConfiguration(dev("land-xhdpi"));
  EXPECT_EQ(whereFor(resources, table, kDensityXxhdpi), "land-xxhdpi");
  resources.setConfiguration(dev("port-xhdpi"));
  EXPECT_EQ(whereFor(resources, table, kDensityXxhdpi), "xxhdpi");

  // A table assigned another, of the same resource but other folders: what was picked for a
  // density was of the one that is gone.
  {
    const TempDir otherDir;
    ResourceTable other;
    addPackage(other, otherDir, {"hdpi", "xhdpi"});
    table = std::move(other);
  }
  EXPECT_TRUE(isOfTheTable(resources.candidateForDensity(id, kDensityXxhdpi), table, id));
  EXPECT_EQ(whereFor(resources, table, kDensityXxhdpi), "xhdpi");
}

// ---- the locale of the device ----------------------------------------------------------------

TEST(ResourcesLocale, TheDevicesOwnWithoutAList) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko", "en-rGB"});
  Resources resources(table);
  resources.setConfiguration(dev("ko-rKR"));
  EXPECT_EQ(chosen(resources), "ko-rKR");
  EXPECT_EQ(where(resources, table), "ko");
}

TEST(ResourcesLocale, ASingleLocaleIsTakenAsItIs) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko"});
  Resources resources(table);
  // It replaces the locale of the device, whether the table has it or not.
  resources.setConfiguration(dev("ko-rKR-night"), tags({"ja-JP"}));
  EXPECT_EQ(chosen(resources), "ja-rJP-night");
  EXPECT_EQ(where(resources, table), "default");
}

TEST(ResourcesLocale, FirstOfTheListTheTableHas) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko", "b+zh+Hant", "es-rMX"});
  Resources resources(table);

  resources.setConfiguration(Config{}, tags({"ja-JP", "ko-KR", "fr-FR"}));
  EXPECT_EQ(chosen(resources), "ko-rKR");
  EXPECT_EQ(where(resources, table), "ko");

  // By language and script, not region: es-AR is the Spanish of values-es-rMX.
  resources.setConfiguration(Config{}, tags({"ja-JP", "es-AR", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "es-rAR");
  EXPECT_EQ(where(resources, table), "es-rMX");

  // zh-CN is Hans, which the table has not; zh-HK is Hant.
  resources.setConfiguration(Config{}, tags({"zh-CN", "zh-HK", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "zh-rHK");
  EXPECT_EQ(where(resources, table), "b+zh+Hant");

  // The first is one the table has: no need to look further.
  resources.setConfiguration(Config{}, tags({"ko-KR", "es-ES"}));
  EXPECT_EQ(chosen(resources), "ko-rKR");
}

TEST(ResourcesLocale, EnglishIsTakenToBeThere) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko"});
  Resources resources(table);

  // An app keeps its English without a language, so English counts though no value is of it:
  // it comes before Korean here, and the values are the default ones.
  resources.setConfiguration(Config{}, tags({"fr-FR", "en-GB", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "en-rGB");
  EXPECT_EQ(where(resources, table), "default");

  resources.setConfiguration(Config{}, tags({"en-US", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "en-rUS");

  // Not en-XA, which is no English.
  resources.setConfiguration(Config{}, tags({"fr-FR", "en-XA", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "ko-rKR");
}

TEST(ResourcesLocale, FirstOfTheListWhereTheTableHasNone) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko"});
  Resources resources(table);
  resources.setConfiguration(Config{}, tags({"fr-FR", "ja-JP"}));
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(where(resources, table), "default");
}

TEST(ResourcesLocale, ATableWithoutLocalesChoosesNothing) {
  for (const char* pseudo : {"en-rXA", "ar-rXB"}) {
    SCOPED_TRACE(pseudo);
    TempDir dir;
    ResourceTable table;
    addPackage(table, dir, {"night", pseudo});
    Resources resources(table);
    // Not even English: with no locales but the pseudo-locales, Android does not look.
    resources.setConfiguration(Config{}, tags({"fr-FR", "en-US"}));
    EXPECT_EQ(chosen(resources), "fr-rFR");
  }

  // en-XA in a script or of a variant is no pseudo-locale but English, so the table has a locale
  // and English counts.
  for (const char* english : {"b+en+Latn+XA", "b+en+XA+posix"}) {
    SCOPED_TRACE(english);
    TempDir dir;
    ResourceTable table;
    addPackage(table, dir, {"night", english});
    Resources resources(table);
    resources.setConfiguration(Config{}, tags({"fr-FR", "en-US"}));
    EXPECT_EQ(chosen(resources), "en-rUS");
  }
}

TEST(ResourcesLocale, TheSystemsLocalesWhereTheAppHasNone) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko", "ja"}, "android", kFrameworkPackageId);
  addPackage(table, dir, {"night"});
  Resources resources(table);
  resources.setConfiguration(Config{}, tags({"fr-FR", "ja-JP", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "ja-rJP");
  EXPECT_EQ(where(resources, table, "android"), "ja");

  // Once the app has a locale of its own, only the app's count, and what was picked for the old
  // locale is picked again.
  addPackage(table, dir, {"ko"}, "com.example.lib", 0x7e);
  EXPECT_EQ(chosen(resources), "ko-rKR");
  EXPECT_EQ(where(resources, table, "android"), "ko");
}

// The locale is chosen once for the locales and the table, and kept while the rest of the device
// changes (#206); chosen again when the locales are others, or the table is. This sees that what
// is kept is right; that the table is not gone through again, ResourcesAllocTest.cpp.
TEST(ResourcesLocale, KeepsTheChoiceUntilTheLocalesOrTheTableChange) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"ko", "ko-night", "night"});
  Resources resources(table);
  const std::vector<Config> frKo = tags({"fr-FR", "ko-KR"});

  resources.setConfiguration(dev("night"), frKo);
  EXPECT_EQ(chosen(resources), "ko-rKR-night");
  EXPECT_EQ(where(resources, table), "ko-night");
  const uint32_t night = resources.generation();
  resources.setConfiguration(dev("night"), frKo);
  EXPECT_EQ(resources.generation(), night);

  // Only the device changes: the same locale, and the values of it.
  resources.setConfiguration(dev("land-notnight"), frKo);
  EXPECT_NE(resources.generation(), night);
  EXPECT_EQ(chosen(resources), "ko-rKR-land-notnight");
  EXPECT_EQ(where(resources, table), "ko");

  // Other locales, of the same table.
  resources.setConfiguration(dev("night"), tags({"fr-FR", "ja-JP"}));
  EXPECT_EQ(chosen(resources), "fr-rFR-night");
  EXPECT_EQ(where(resources, table), "night");
  resources.setConfiguration(dev("ja-rJP-night"));
  EXPECT_EQ(chosen(resources), "ja-rJP-night");
  resources.setConfiguration(dev("night"), frKo);
  EXPECT_EQ(chosen(resources), "ko-rKR-night");

  // The same locales, of a table that has French now: setConfiguration sees it before anything
  // else asks.
  addPackage(table, dir, {"fr"}, "com.example.lib", 0x7e);
  resources.setConfiguration(dev("night"), frKo);
  EXPECT_EQ(chosen(resources), "fr-rFR-night");
  EXPECT_EQ(where(resources, table), "night");
  EXPECT_EQ(where(resources, table, "com.example.lib"), "fr");
}

TEST(ResourcesLocale, OldLanguageCodes) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"iw", "he", "in", "ko"});
  Resources resources(table);

  // Hebrew is iw to Android's resources, whichever code the device gives.
  resources.setConfiguration(dev("he-rIL"));
  EXPECT_EQ(chosen(resources), "iw-rIL");
  EXPECT_EQ(where(resources, table), "iw");
  resources.setConfiguration(dev("iw-rIL"));
  EXPECT_EQ(where(resources, table), "iw");
  resources.setConfiguration(Config{}, tags({"id-ID"}));
  EXPECT_EQ(chosen(resources), "in-rID");
  EXPECT_EQ(where(resources, table), "in");

  // In a list, values-iw and values-he both make Hebrew a language the table has.
  resources.setConfiguration(Config{}, tags({"fr-FR", "he-IL", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "iw-rIL");
  EXPECT_EQ(where(resources, table), "iw");
}

TEST(ResourcesLocale, OldLanguageCodesAloneInAList) {
  // As apps and the framework have them (golden/aosp/framework_res_dirs.txt): values-iw/ and no
  // values-he/, so a list finds Hebrew in the table only by reading iw as he.
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"iw", "in", "ji", "ko"});
  Resources resources(table);

  resources.setConfiguration(Config{}, tags({"fr-FR", "he-IL", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "iw-rIL");
  EXPECT_EQ(where(resources, table), "iw");
  resources.setConfiguration(Config{}, tags({"fr-FR", "id-ID", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "in-rID");
  EXPECT_EQ(where(resources, table), "in");
  resources.setConfiguration(Config{}, tags({"fr-FR", "yi", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "ji");
  EXPECT_EQ(where(resources, table), "ji");
  // And a list that gives the old code finds it too, by reading its own iw as he.
  resources.setConfiguration(Config{}, tags({"fr-FR", "iw-IL", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "iw-rIL");
  EXPECT_EQ(where(resources, table), "iw");
}

TEST(ResourcesLocale, TagalogIsFilipinoInAList) {
  for (const char* folder : {"tl", "tl-rPH"}) {
    SCOPED_TRACE(folder);
    TempDir dir;
    ResourceTable table;
    addPackage(table, dir, {folder, "ko"});
    Resources resources(table);
    // The table's Tagalog is Filipino to the list, which is how a device names it.
    resources.setConfiguration(Config{}, tags({"fr-FR", "fil-PH", "ko-KR"}));
    EXPECT_EQ(chosen(resources), "fil-rPH");
    EXPECT_EQ(where(resources, table), folder);
    // And tl in the list is then no language the table has, as on Android (golden/data,
    // locales_ja-JP+tl-PH+ko-KR), though alone it takes the values of values-tl/.
    resources.setConfiguration(Config{}, tags({"fr-FR", "tl-PH", "ko-KR"}));
    EXPECT_EQ(chosen(resources), "ko-rKR");
    resources.setConfiguration(Config{}, tags({"tl-PH"}));
    EXPECT_EQ(where(resources, table), folder);
  }
}

TEST(ResourcesLocale, ALanguageWithoutAKnownScriptGoesByRegion) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"qaa-rFR", "ko"});
  Resources resources(table);
  resources.setConfiguration(Config{}, tags({"ja-JP", "qaa-CA", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "ko-rKR");
  resources.setConfiguration(Config{}, tags({"ja-JP", "qaa-FR", "ko-KR"}));
  EXPECT_EQ(chosen(resources), "qaa-rFR");

  // A folder of the language without a region is of every region of it.
  TempDir otherDir;
  ResourceTable other;
  addPackage(other, otherDir, {"qaa", "ko"});
  Resources any(other);
  any.setConfiguration(Config{}, tags({"ja-JP", "qaa-CA", "ko-KR"}));
  EXPECT_EQ(chosen(any), "qaa-rCA");
  EXPECT_EQ(where(any, other), "qaa");
}

TEST(ResourcesLocale, KeepsTheScriptAndVariantOfTheChosen) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"sr", "b+sr+Latn"});
  Resources resources(table);
  resources.setConfiguration(dev("night"), tags({"ja-JP", "sr-Latn-RS"}));
  EXPECT_EQ(chosen(resources), "b+sr+Latn+RS-night");
  EXPECT_EQ(where(resources, table), "b+sr+Latn");
  resources.setConfiguration(dev("night"), tags({"ja-JP", "sr-RS"}));
  EXPECT_EQ(where(resources, table), "sr");
}

// ---- the app's default locale (LocaleConfig) -------------------------------------------------

// A package of strings, each with a value in each folder that names it, by the qualifiers of the
// folder ("" for values/): the value is those qualifiers, or "default".
using Folders = std::vector<std::pair<std::string, std::vector<std::string>>>;

void addStrings(ResourceTable& table, const TempDir& dir, const Folders& folders) {
  const std::filesystem::path res = dir.path() / "res";
  for (const auto& [qualifiers, names] : folders) {
    const std::string folder = qualifiers.empty() ? "values" : "values-" + qualifiers;
    std::filesystem::create_directories(res / folder);
    std::ofstream out(res / folder / "strings.xml", std::ios::binary);
    out << "<resources>";
    for (const std::string& name : names) {
      out << "<string name=\"" << name << "\">" << (qualifiers.empty() ? "default" : qualifiers)
          << "</string>";
    }
    out << "</resources>";
  }
  std::vector<Diagnostic> errors;
  ASSERT_TRUE(table.addPackage(res, "com.example", kAppPackageId, &errors))
      << (errors.empty() ? "" : errors[0].message);
}

// The value Resources takes for the string `name`, as addStrings made it.
std::string from(const Resources& resources, const ResourceTable& table, const std::string& name,
                 uint16_t density = 0) {
  const std::optional<ResId> id = table.find("com.example", ResType::String, name);
  if (!id) return "no resource";
  const Candidate* c = resources.candidateForDensity(*id, density);
  if (c == nullptr) return "none";
  return table.package("com.example")->strings.at(c->value.index());
}

LocaleConfig localeConfig(const std::vector<const char*>& supported, const char* defaultLocale) {
  LocaleConfig config;
  config.supported = tags(supported);
  if (defaultLocale != nullptr) config.defaultLocale = tags({defaultLocale})[0];
  return config;
}

TEST(ResourcesLocaleConfig, EachResourceTakesTheFirstLocaleItHas) {
  TempDir dir;
  ResourceTable table;
  addStrings(
      table, dir,
      {{"", {"all", "es", "none"}}, {"fr", {"all"}}, {"es", {"all", "es"}}, {"de", {"all"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"en-US", "fr", "es"}, "en-US"));
  // de is not one the app supports, so fr and es are read in, fr first.
  resources.setConfiguration(Config{}, tags({"de-DE", "fr-FR", "es-ES"}));
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "all"), "fr");
  EXPECT_EQ(from(resources, table, "es"), "es");
  EXPECT_EQ(from(resources, table, "none"), "default");

  // Without the default, one language for the whole table, the first it has, from the next update
  // on: the locales are not those the configuration was left with, fr and es.
  resources.setLocaleConfig(localeConfig({"en-US", "fr", "es"}, nullptr));
  EXPECT_EQ(chosen(resources), "fr-rFR");
  resources.setConfiguration(Config{}, tags({"de-DE", "fr-FR", "es-ES"}));
  EXPECT_EQ(chosen(resources), "de-rDE");
  EXPECT_EQ(from(resources, table, "all"), "de");
  EXPECT_EQ(from(resources, table, "es"), "default");
}

TEST(ResourcesLocaleConfig, ValuesWithoutALanguageAreOfTheDefault) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"es"}}, {"es", {"es"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"en-US", "fr", "es"}, "en-US"));
  // values/ is of en-US, which comes before es in the list.
  resources.setConfiguration(Config{}, tags({"fr-FR", "en-US", "es-ES"}));
  EXPECT_EQ(from(resources, table, "es"), "default");
  // Not so when English comes after.
  resources.setConfiguration(Config{}, tags({"fr-FR", "es-ES", "en-US"}));
  EXPECT_EQ(from(resources, table, "es"), "es");

  // A default under a new code is that of the locale read in under the old one: he is iw.
  TempDir hebrewDir;
  ResourceTable hebrew;
  addStrings(hebrew, hebrewDir, {{"", {"x"}}, {"fr", {"x"}}});
  Resources ofHebrew(hebrew);
  ofHebrew.setLocaleConfig(localeConfig({"he", "fr"}, "he"));
  ofHebrew.setConfiguration(Config{}, tags({"he-IL", "fr-FR"}));
  EXPECT_EQ(chosen(ofHebrew), "iw-rIL");
  EXPECT_EQ(from(ofHebrew, hebrew, "x"), "default");
}

TEST(ResourcesLocaleConfig, NoValueForOneLocaleIsNoValue) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"fr", {"fr"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "fr"));
  // AssetManager2::FindEntry gives up as soon as one locale finds nothing.
  resources.setConfiguration(Config{}, tags({"fr-FR", "es-ES"}));
  EXPECT_EQ(from(resources, table, "fr"), "none");
  // With fr alone the app supports, as a single locale is read, fr it is.
  resources.setConfiguration(Config{}, tags({"fr-FR", "de-DE"}));
  EXPECT_EQ(from(resources, table, "fr"), "fr");
}

TEST(ResourcesLocaleConfig, AnMccComesBeforeTheLocale) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x"}}, {"fr", {"x"}}, {"mcc310-es", {"x"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "fr"));
  Config device = dev("mcc310");
  resources.setConfiguration(device, tags({"fr-FR", "es-ES"}));
  EXPECT_EQ(from(resources, table, "x"), "mcc310-es");
  // A device of another mcc has values-fr/.
  device.mcc = 208;
  resources.setConfiguration(device, tags({"fr-FR", "es-ES"}));
  EXPECT_EQ(from(resources, table, "x"), "fr");
}

TEST(ResourcesLocaleConfig, AsWithoutADefaultWhereNoneTheAppSupportsIsThere) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x"}}, {"es", {"x"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"en-US"}, "en-US"));
  // The app supports none of these, so one is chosen for the whole table: the first it has.
  resources.setConfiguration(Config{}, tags({"fr-FR", "es-ES"}));
  EXPECT_EQ(chosen(resources), "es-rES");
  EXPECT_EQ(from(resources, table, "x"), "es");
  // Or the first if it has none.
  resources.setConfiguration(Config{}, tags({"fr-FR", "ja-JP"}));
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "x"), "default");
  // And a single locale is taken as it is.
  resources.setConfiguration(Config{}, tags({"ja-JP"}));
  EXPECT_EQ(chosen(resources), "ja-rJP");
}

TEST(ResourcesLocaleConfig, GenerationChangesWithWhatIsPicked) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x"}}, {"es", {"x"}}});
  Resources resources(table);
  const std::vector<Config> frEs = tags({"fr-FR", "es-ES"});
  resources.setConfiguration(Config{}, frEs);
  EXPECT_EQ(from(resources, table, "x"), "es");
  uint32_t before = resources.generation();

  // A LocaleConfig changes nothing until the next update, as on Android.
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "fr"));
  EXPECT_EQ(resources.generation(), before);
  EXPECT_EQ(from(resources, table, "x"), "es");
  // Which reads in fr and es: the configuration is fr's, and values/ is fr's too.
  resources.setConfiguration(Config{}, frEs);
  EXPECT_NE(resources.generation(), before);
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "x"), "default");

  // Another default, with the same locales read in, picks otherwise.
  before = resources.generation();
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "es"));
  resources.setConfiguration(Config{}, frEs);
  EXPECT_NE(resources.generation(), before);
  EXPECT_EQ(from(resources, table, "x"), "es");

  // The same again changes nothing.
  before = resources.generation();
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "es"));
  resources.setConfiguration(Config{}, frEs);
  EXPECT_EQ(resources.generation(), before);

  // Nor does another default with one locale read in.
  resources.setConfiguration(Config{}, tags({"es-ES"}));
  before = resources.generation();
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "fr"));
  resources.setConfiguration(Config{}, tags({"es-ES"}));
  EXPECT_EQ(resources.generation(), before);
}

// ResourcesImpl.updateConfiguration, which, having chosen one locale for the whole table though
// the app has a default, puts it first in the configuration's locales, and reads in all of them
// at an update that leaves them as they are (ResourcesImpl.java 564-598, Configuration.updateFrom).
TEST(ResourcesLocaleConfig, OneLocaleChosenFirstWithADefaultIsWidenedByTheNextCall) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x", "y"}}, {"es", {"x"}}, {"fr", {"y"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"en-US"}, "en-US"));
  const std::vector<Config> list = tags({"fr-FR", "de-DE", "es-ES"});
  // The app supports none of these, and the first the table has is fr, the first of them.
  resources.setConfiguration(Config{}, list);
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "x"), "default");
  const uint32_t before = resources.generation();

  // A call of the same locales, the device's rotated, reads in fr, de, es.
  resources.setConfiguration(dev("land"), list);
  EXPECT_NE(resources.generation(), before);
  EXPECT_EQ(chosen(resources), "fr-rFR-land");
  EXPECT_EQ(from(resources, table, "x"), "es");
  EXPECT_EQ(from(resources, table, "y"), "fr");
  // And so on after it.
  resources.setConfiguration(dev("port"), list);
  EXPECT_EQ(from(resources, table, "x"), "es");

  // Other locales are chosen afresh.
  resources.setConfiguration(Config{}, tags({"de-DE", "es-ES"}));
  EXPECT_EQ(chosen(resources), "es-rES");
  EXPECT_EQ(from(resources, table, "y"), "default");
}

TEST(ResourcesLocaleConfig, OneLocaleChosenLaterWithADefaultIsWidenedByTheLocalesItLeft) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x", "y"}}, {"es", {"x"}}, {"fr", {"y"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"en-US"}, "en-US"));
  // fr is chosen, not the first: Android makes the configuration's locales fr, de, es.
  const std::vector<Config> list = tags({"de-DE", "fr-FR", "es-ES"});
  resources.setConfiguration(Config{}, list);
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "x"), "default");
  // So the same locales again are a change to it, and fr is chosen again, alone.
  resources.setConfiguration(dev("land"), list);
  EXPECT_EQ(chosen(resources), "fr-rFR-land");
  EXPECT_EQ(from(resources, table, "x"), "default");
  resources.setConfiguration(dev("port"), list);
  EXPECT_EQ(from(resources, table, "x"), "default");
  // And those it made are none, and all of them are read in.
  resources.setConfiguration(dev("port"), tags({"fr-FR", "de-DE", "es-ES"}));
  EXPECT_EQ(chosen(resources), "fr-rFR-port");
  EXPECT_EQ(from(resources, table, "x"), "es");
  EXPECT_EQ(from(resources, table, "y"), "fr");
}

// The locales a choice left are those of that choice: once the table has changed and they are
// chosen afresh, the ones it left before are a change like any other.
TEST(ResourcesLocaleConfig, AChangedTableForgetsTheLocalesItLeft) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x", "y"}}, {"es", {"x"}}, {"fr", {"y"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"en-US"}, "en-US"));
  // fr, not the first, is chosen: Android would leave fr, de, es.
  resources.setConfiguration(Config{}, tags({"de-DE", "fr-FR", "es-ES"}));
  EXPECT_EQ(chosen(resources), "fr-rFR");
  // Now the table has de, so de, the first, is chosen afresh, and leaves them as they are.
  addPackage(table, dir, {"de"}, "com.example.lib", 0x7e);
  EXPECT_EQ(chosen(resources), "de-rDE");
  // fr, de, es are then a change, of which fr, the first, is chosen alone: x is not es's.
  resources.setConfiguration(Config{}, tags({"fr-FR", "de-DE", "es-ES"}));
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "x"), "default");
}

// Whether a call that reads comes between, the next call after the table has changed is the
// same: the table is taken in first.
TEST(ResourcesLocaleConfig, AChangedTableIsTakenInBeforeTheCall) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x"}}, {"es", {"x"}}});
  Resources read(table);
  Resources unread(table);
  const std::vector<Config> list = tags({"fr-FR", "es-ES"});
  for (Resources* resources : {&read, &unread}) {
    resources->setLocaleConfig(localeConfig({"en-US"}, "en-US"));
    resources->setConfiguration(Config{}, list);
    EXPECT_EQ(chosen(*resources), "es-rES");
  }
  // Now the table has fr, the first of the locales, which is chosen anew, alone.
  addPackage(table, dir, {"fr"}, "com.example.lib", 0x7e);
  EXPECT_EQ(chosen(read), "fr-rFR");
  for (Resources* resources : {&read, &unread}) {
    resources->setConfiguration(Config{}, list);
    EXPECT_EQ(chosen(*resources), "fr-rFR");
    EXPECT_EQ(from(*resources, table, "x"), "es");
  }
}

// A LocaleConfig given waits for setConfiguration, even where the table changes before it: the
// locales are chosen again without one, as by a new ResourcesImpl.
TEST(ResourcesLocaleConfig, AChangedTableTakesNotALocaleConfigGivenSince) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x"}}, {"es", {"x"}}});
  Resources resources(table);
  const std::vector<Config> frEs = tags({"fr-FR", "es-ES"});
  resources.setConfiguration(Config{}, frEs);
  EXPECT_EQ(chosen(resources), "es-rES");
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "es"));

  // The table has fr now: fr is chosen of the two, alone, as without a LocaleConfig.
  addPackage(table, dir, {"fr"}, "com.example.lib", 0x7e);
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(from(resources, table, "x"), "default");
  // The next call takes it in: the same locales, now the configuration's, are all read in.
  resources.setConfiguration(Config{}, frEs);
  EXPECT_EQ(from(resources, table, "x"), "es");
}

// A table that changes has one locale chosen, as Android's new ResourcesImpl does, and the app's
// default taken in again at the next call.
TEST(ResourcesLocaleConfig, ChosenAgainForAChangedTable) {
  TempDir dir;
  ResourceTable table;
  addPackage(table, dir, {"night"});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "fr"));
  const std::vector<Config> frEs = tags({"fr-FR", "es-ES"});
  resources.setConfiguration(Config{}, frEs);
  EXPECT_EQ(chosen(resources), "fr-rFR");

  // A new ResourcesImpl, of the new assets, chooses before it has the LocaleConfig: es, the one
  // the table has, alone (ResourcesManager, Resources.setImpl).
  addPackage(table, dir, {"es"}, "com.example.lib", 0x7e);
  EXPECT_EQ(chosen(resources), "es-rES");
  EXPECT_EQ(where(resources, table, "com.example.lib"), "es");
  // The next call takes the LocaleConfig in: fr, es are not es, fr, so the supported of them.
  resources.setConfiguration(Config{}, frEs);
  EXPECT_EQ(chosen(resources), "fr-rFR");
  EXPECT_EQ(where(resources, table, "com.example.lib"), "default");
}

TEST(ResourcesLocaleConfig, ADensityForEachLocale) {
  TempDir dir;
  ResourceTable table;
  addStrings(table, dir, {{"", {"x"}}, {"es", {"x"}}, {"es-hdpi", {"x"}}});
  Resources resources(table);
  resources.setLocaleConfig(localeConfig({"fr", "es"}, "es"));
  resources.setConfiguration(dev("mdpi"), tags({"fr-FR", "es-ES"}));
  EXPECT_EQ(from(resources, table, "x"), "es");
  EXPECT_EQ(from(resources, table, "x", 240), "es-hdpi");
}

// ---- parseLocaleConfig -----------------------------------------------------------------------

std::filesystem::path writeFile(const TempDir& dir, const std::string& text) {
  const std::filesystem::path file = dir.path() / "locales_config.xml";
  std::ofstream(file, std::ios::binary) << text;
  return file;
}

constexpr const char* kAndroid = "xmlns:android=\"http://schemas.android.com/apk/res/android\"";

TEST(ParseLocaleConfig, ReadsTheLocalesAndTheDefault) {
  TempDir dir;
  const std::string text = std::string("<locale-config ") + kAndroid +
                           " android:defaultLocale=\"en-US\">"
                           "<locale android:name=\"en-US\"/><locale android:name=\"fr\"/>"
                           "<other><locale android:name=\"de\"/></other>"
                           "<locale android:name=\"fr\"/><locale android:name=\"zh-Hant-TW\"/>"
                           "</locale-config>";
  std::vector<Diagnostic> errors;
  const std::optional<LocaleConfig> config = parseLocaleConfig(writeFile(dir, text), &errors);
  ASSERT_TRUE(config) << (errors.empty() ? "" : errors[0].message);
  // Of the root's own <locale>s, each once.
  ASSERT_EQ(config->supported.size(), 3u);
  EXPECT_EQ(toString(config->supported[0]), "en-rUS");
  EXPECT_EQ(toString(config->supported[1]), "fr");
  EXPECT_EQ(toString(config->supported[2]), "b+zh+Hant+TW");
  ASSERT_TRUE(config->defaultLocale);
  EXPECT_EQ(toString(*config->defaultLocale), "en-rUS");

  const std::optional<LocaleConfig> none =
      parseLocaleConfig(writeFile(dir, std::string("<locale-config ") + kAndroid +
                                           "><locale android:name=\"fr\"/></locale-config>"));
  ASSERT_TRUE(none);
  EXPECT_FALSE(none->defaultLocale);
}

TEST(ParseLocaleConfig, ReadsANameAsLocaleForLanguageTagDoes) {
  TempDir dir;
  // Up to the first subtag that is not well-formed; nothing of a name with no language, or of no
  // name, which LocaleConfig keeps its default with all the same.
  const std::string text = std::string("<locale-config ") + kAndroid +
                           " android:defaultLocale=\"fr-FR-ab\"><locale/>"
                           "<locale android:name=\"not a tag\"/><locale android:name=\"fr-FR-ab\"/>"
                           "<locale android:name=\"de-DE-1996-x\"/></locale-config>";
  std::vector<Diagnostic> errors;
  const std::optional<LocaleConfig> config = parseLocaleConfig(writeFile(dir, text), &errors);
  ASSERT_TRUE(config) << (errors.empty() ? "" : errors[0].message);
  EXPECT_TRUE(errors.empty());
  ASSERT_EQ(config->supported.size(), 2u);
  EXPECT_EQ(toString(config->supported[0]), "fr-rFR");
  EXPECT_EQ(toString(config->supported[1]), "b+de+DE+1996");
  ASSERT_TRUE(config->defaultLocale);
  EXPECT_EQ(toString(*config->defaultLocale), "fr-rFR");

  // A default of no language is und, as ResourcesImpl gives it.
  const std::optional<LocaleConfig> und = parseLocaleConfig(
      writeFile(dir, std::string("<locale-config ") + kAndroid +
                         " android:defaultLocale=\"not a tag\"><locale android:name=\"not a tag\"/>"
                         "<locale android:name=\"fr\"/></locale-config>"));
  ASSERT_TRUE(und);
  ASSERT_TRUE(und->defaultLocale);
  EXPECT_EQ(toString(*und->defaultLocale), "und");

  // A name of several, as LocaleConfig joins the names with ',' and LocaleList splits them, each
  // locale once; the default is still the name as written.
  std::vector<Diagnostic> commaErrors;
  const std::optional<LocaleConfig> several = parseLocaleConfig(
      writeFile(dir, std::string("<locale-config ") + kAndroid +
                         " android:defaultLocale=\"fr\"><locale android:name=\"fr,de-DE\"/>"
                         "<locale android:name=\"de-DE\"/><locale android:name=\"fr\"/>"
                         "</locale-config>"),
      &commaErrors);
  ASSERT_TRUE(several) << (commaErrors.empty() ? "" : commaErrors[0].message);
  ASSERT_EQ(several->supported.size(), 2u);
  EXPECT_EQ(toString(several->supported[0]), "fr");
  EXPECT_EQ(toString(several->supported[1]), "de-rDE");
  EXPECT_FALSE(parseLocaleConfig(writeFile(
      dir, std::string("<locale-config ") + kAndroid +
               " android:defaultLocale=\"fr\"><locale android:name=\"fr,de\"/></locale-config>")));
}

TEST(ParseLocaleConfig, FailsAsLocaleConfigDoes) {
  TempDir dir;
  const std::string open = std::string("<locale-config ") + kAndroid;
  for (const std::string& text : {
           // A default that is not, as written, one of the names.
           open +
               " android:defaultLocale=\"en-us\"><locale android:name=\"en-US\"/></locale-config>",
           open + " android:defaultLocale=\"de\"><locale android:name=\"fr\"/></locale-config>",
           // Another root.
           std::string("<locales ") + kAndroid + "><locale android:name=\"fr\"/></locales>",
           // No XML.
           std::string("<locale-config"),
       }) {
    SCOPED_TRACE(text);
    std::vector<Diagnostic> errors;
    EXPECT_FALSE(parseLocaleConfig(writeFile(dir, text), &errors));
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_FALSE(errors[0].message.empty());
  }
  std::vector<Diagnostic> errors;
  EXPECT_FALSE(parseLocaleConfig(dir.path() / "missing.xml", &errors));
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "failed to open file");
}

}  // namespace
}  // namespace hydroid::res

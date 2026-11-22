// What Resources allocates, as the operator new of this program counts it (#240): that a call
// that changes only the device does not go through the table again (#206), and that one whose
// allocation fails leaves all as it was. A program of its own, as hydroid-res-tests keeps the
// operator new of the sanitizers, which checks that each delete is of a new.

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "TempDir.h"
#include "hydroid/res/ConfigParser.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/Resources.h"
#include "hydroid_res.h"

namespace {

// Allocations are counted while `counting`, from 0, and the one of the count `failing` throws.
std::atomic<bool> counting{false};
std::atomic<long> allocations{0};
std::atomic<long> failing{-1};

// pugixml's allocations, which it makes with malloc but for these (#263): counted while
// `counting`, apart from operator new's, and the one of the count `pugiFailing` given no memory,
// which pugixml says by an empty node or false rather than by throwing.
std::atomic<long> pugiAllocations{0};
std::atomic<long> pugiFailing{-1};

void* pugiAllocate(size_t size) {
  if (counting.load() && pugiAllocations.fetch_add(1) == pugiFailing.load()) return nullptr;
  return std::malloc(size);
}
void pugiDeallocate(void* p) { std::free(p); }

// Before main, and before anything pugixml allocates, which pugiDeallocate frees all the same.
[[maybe_unused]] const bool kPugiCounted = [] {
  pugi::set_memory_management_functions(pugiAllocate, pugiDeallocate);
  return true;
}();

}  // namespace

// Out of line, as GCC's optimizer calls it a mismatch when it inlines one of them into a test
// (-Wmismatched-new-delete, #258): a free there of what a call to operator new gave, or a call to
// operator delete there of what a malloc gave. With neither inlined, it sees the calls pair up.
#if defined(__GNUC__)
#define HYDROID_OUT_OF_LINE __attribute__((noinline))
#else
#define HYDROID_OUT_OF_LINE
#endif

HYDROID_OUT_OF_LINE void* operator new(std::size_t size) {
  if (counting.load() && allocations.fetch_add(1) == failing.load()) throw std::bad_alloc();
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
HYDROID_OUT_OF_LINE void operator delete(void* p) noexcept { std::free(p); }
HYDROID_OUT_OF_LINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace hydroid::res {
namespace {

using test::TempDir;

// The allocations `f` makes, of which the one `failAt` throws std::bad_alloc, if it is not -1.
template <typename F>
long allocationsOf(F&& f, long failAt = -1) {
  struct Stop {
    ~Stop() {
      counting = false;
      failing = -1;
    }
  } stop;
  allocations = 0;
  failing = failAt;
  counting = true;
  f();
  return allocations.load();
}

// The allocations pugixml makes as `f` runs, of which the one `failAt` gets no memory, if it is
// not -1; operator new fails none.
template <typename F>
long pugiAllocationsOf(F&& f, long failAt = -1) {
  struct Stop {
    ~Stop() {
      counting = false;
      pugiFailing = -1;
    }
  } stop;
  pugiAllocations = 0;
  pugiFailing = failAt;
  counting = true;
  f();
  return pugiAllocations.load();
}

// Stops the test where the library allocates out of sight of the operator new above, as a DLL,
// which has its own, does: what is counted would then be nothing, whatever the library did.
#define REQUIRE_COUNTED(n)                                      \
  do {                                                          \
    if ((n) == 0) {                                             \
      if constexpr (HYDROID_SHARED_LIBRARY) {                   \
        GTEST_SKIP() << "the library has its own operator new"; \
      } else {                                                  \
        FAIL() << "the library's allocations are not counted";  \
      }                                                         \
    }                                                           \
  } while (0)

// The checked iterators of MSVC's Debug library allocate in constructors that are noexcept, as an
// empty vector's, so a failed allocation there ends the program, wherever it is made. The tests
// that fail one are left out of such a build, as MSVC finds the code after a skip unreachable.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
#define HYDROID_NOEXCEPT_ALLOCATES 1
#else
#define HYDROID_NOEXCEPT_ALLOCATES 0
#endif

Config dev(const std::string& qualifiers) {
  Config c = parseConfig(qualifiers).value_or(Config{});
  c.sdkVersion = 0;
  return c;
}

std::vector<Config> tags(const std::vector<const char*>& languageTags) {
  std::vector<Config> locales;
  for (const char* tag : languageTags) locales.push_back(parseLanguageTag(tag).value_or(Config{}));
  return locales;
}

// A res/ folder `name` of a string in values/ and each values-<qualifiers>/ of `folders`.
std::filesystem::path writePackage(const TempDir& dir, const std::vector<const char*>& folders,
                                   const std::string& name) {
  const std::filesystem::path res = dir.path() / name;
  std::vector<std::string> all = {"values"};
  for (const char* folder : folders) all.push_back(std::string("values-") + folder);
  for (const std::string& folder : all) {
    std::filesystem::create_directories(res / folder);
    std::ofstream(res / folder / "strings.xml", std::ios::binary)
        << "<resources><string name=\"where\">" << folder << "</string></resources>";
  }
  return res;
}

// The allocations of adding a package of writePackage, which are all the library's.
long addPackage(ResourceTable& table, const TempDir& dir, const std::vector<const char*>& folders,
                const std::string& name = "com.example", uint8_t id = kAppPackageId) {
  const std::filesystem::path res = writePackage(dir, folders, name);
  bool added = false;
  const long n = allocationsOf([&] { added = table.addPackage(res, name, id); });
  EXPECT_TRUE(added) << name;
  return n;
}

// The locale Resources chose, as a folder would name it.
std::string chosen(const Resources& resources) {
  Config c = resources.configuration();
  c.sdkVersion = 0;
  return toString(c);
}

TEST(ResourcesAllocations, OnlyTheDeviceChangingAllocatesNothing) {
  TempDir dir;
  ResourceTable table;
  REQUIRE_COUNTED(addPackage(table, dir, {"ko", "ja", "night"}));
  Resources resources(table);
  const std::vector<Config> frKo = tags({"fr-FR", "ko-KR"});
  const Config night = dev("night");
  const Config day = dev("land-notnight");

  // The first call goes through the table for its locales.
  EXPECT_GT(allocationsOf([&] { resources.setConfiguration(night, frKo); }), 0);
  EXPECT_EQ(chosen(resources), "ko-rKR-night");

  // Not those after it, of the same locales and table.
  EXPECT_EQ(allocationsOf([&] { resources.setConfiguration(night, frKo); }), 0);
  EXPECT_EQ(allocationsOf([&] { resources.setConfiguration(day, frKo); }), 0);
  EXPECT_EQ(chosen(resources), "ko-rKR-land-notnight");
}

// The same with the app's default locale, which has more than one locale read in (#199): the
// first call after their number has grown has the room for them too.
TEST(ResourcesAllocations, OnlyTheDeviceChangingAllocatesNothingForSeveralLocales) {
  TempDir dir;
  ResourceTable table;
  REQUIRE_COUNTED(addPackage(table, dir, {"ko", "ja", "night"}));
  Resources resources(table);
  LocaleConfig app;
  app.supported = tags({"fr", "ko", "ja"});
  app.defaultLocale = app.supported[0];
  resources.setLocaleConfig(app);
  const std::vector<Config> frKoJa = tags({"fr-FR", "ko-KR", "ja-JP"});
  const Config night = dev("night");
  const Config day = dev("land-notnight");

  EXPECT_GT(allocationsOf([&] { resources.setConfiguration(night, frKoJa); }), 0);
  EXPECT_EQ(chosen(resources), "fr-rFR-night");
  EXPECT_EQ(allocationsOf([&] { resources.setConfiguration(day, frKoJa); }), 0);
  EXPECT_EQ(allocationsOf([&] { resources.setConfiguration(night, frKoJa); }), 0);
  EXPECT_EQ(chosen(resources), "fr-rFR-night");
}

// For each allocation of a setConfiguration that chooses a locale, a call in which it fails:
// what Resources answers is as before, it keeps not the device of the call, and the same call
// again chooses.
TEST(ResourcesAllocations, AFailedChoiceLeavesAllAsItWas) {
#if HYDROID_NOEXCEPT_ALLOCATES
  GTEST_SKIP() << "the checked iterators allocate where they cannot throw";
#else
  TempDir dir;
  const Config night = dev("night");
  const Config day = dev("land-notnight");
  const std::vector<Config> fr = tags({"fr-FR"});
  const std::vector<Config> frKo = tags({"fr-FR", "ko-KR"});
  for (long failAt = 0;; failAt++) {
    SCOPED_TRACE(failAt);
    ResourceTable table;
    REQUIRE_COUNTED(addPackage(table, dir, {"ko"}));
    Resources resources(table);
    // One locale, which is not chosen: the table's are not gathered yet.
    resources.setConfiguration(night, fr);
    const Config before = resources.configuration();
    const uint32_t generation = resources.generation();

    bool threw = false;
    try {
      allocationsOf([&] { resources.setConfiguration(day, frKo); }, failAt);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    if (!threw) {
      EXPECT_GT(failAt, 0) << "the call allocates nothing";
      EXPECT_EQ(chosen(resources), "ko-rKR-land-notnight");
      break;
    }
    EXPECT_EQ(resources.configuration(), before);
    EXPECT_EQ(resources.generation(), generation);
    // Nor the device of the call, which a table that changes would be taken in for.
    addPackage(table, dir, {"night"}, "com.example.lib", 0x7e);
    EXPECT_EQ(chosen(resources), "fr-rFR-night");
    resources.setConfiguration(day, frKo);
    EXPECT_EQ(chosen(resources), "ko-rKR-land-notnight");
  }
#endif
}

// The same after a choice, which the table's locales are gathered for: a call in which an
// allocation fails keeps nothing of its choice, so the locales before, given again, are of the
// choice before.
TEST(ResourcesAllocations, AFailedChoiceKeepsTheChoiceBefore) {
#if HYDROID_NOEXCEPT_ALLOCATES
  GTEST_SKIP() << "the checked iterators allocate where they cannot throw";
#else
  TempDir dir;
  ResourceTable table;
  REQUIRE_COUNTED(addPackage(table, dir, {"ko"}));
  const Config night = dev("night");
  const std::vector<Config> deFr = tags({"de-DE", "fr-FR"});  // de-DE, as the table has neither
  const std::vector<Config> deKo = tags({"de-DE", "ko-KR"});  // ko-KR
  for (long failAt = 0;; failAt++) {
    SCOPED_TRACE(failAt);
    Resources resources(table);
    resources.setConfiguration(night, deFr);
    EXPECT_EQ(chosen(resources), "de-rDE-night");

    bool threw = false;
    try {
      allocationsOf([&] { resources.setConfiguration(night, deKo); }, failAt);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    if (!threw) {
      EXPECT_GT(failAt, 0) << "the call allocates nothing";
      EXPECT_EQ(chosen(resources), "ko-rKR-night");
      break;
    }
    resources.setConfiguration(night, deFr);
    EXPECT_EQ(chosen(resources), "de-rDE-night");
  }
#endif
}

// The same where it is the table that has changed, which the next call to Resources takes in:
// one whose allocation fails leaves the change for the call after it.
TEST(ResourcesAllocations, AFailedChoiceForAChangedTableLeavesItToTheNextCall) {
#if HYDROID_NOEXCEPT_ALLOCATES
  GTEST_SKIP() << "the checked iterators allocate where they cannot throw";
#else
  TempDir dir;
  const Config night = dev("night");
  const std::vector<Config> frKo = tags({"fr-FR", "ko-KR"});
  for (long failAt = 0;; failAt++) {
    SCOPED_TRACE(failAt);
    ResourceTable table;
    REQUIRE_COUNTED(addPackage(table, dir, {"night"}));
    Resources resources(table);
    resources.setConfiguration(night, frKo);
    EXPECT_EQ(chosen(resources), "fr-rFR-night");
    const uint32_t generation = resources.generation();
    addPackage(table, dir, {"ko"}, "com.example.lib", 0x7e);

    bool threw = false;
    try {
      allocationsOf([&] { resources.generation(); }, failAt);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    // Taken in once, by the call or the one after it.
    EXPECT_EQ(resources.generation(), generation + 1);
    EXPECT_EQ(chosen(resources), "ko-rKR-night");
    if (!threw) {
      EXPECT_GT(failAt, 0) << "taking in the table allocates nothing";
      break;
    }
  }
#endif
}

// parseLocaleConfig, which reads one XML file, for each allocation it makes, failing it: what
// fails as expat reads the file throws std::bad_alloc out of it (#256). Every handler of expat is
// read: namespaces, elements with attributes, text and CDATA. It sees an exception that a handler
// keeps and load() does not throw again; one let through expat as well, as the expat it is built
// with unwinds, on MSVC and on CI's GCC alike. Not by addPackage, which on GCC ends the program as
// it lists the folder, in libstdc++'s directory_iterator (#262).
TEST(ResourcesAllocations, AFailedAllocationAsXmlIsReadThrows) {
#if HYDROID_NOEXCEPT_ALLOCATES
  GTEST_SKIP() << "the checked iterators allocate where they cannot throw";
#else
  TempDir dir;
  dir.write("locales_config.xml",
            "<locale-config xmlns:android=\"http://schemas.android.com/apk/res/android\" "
            "android:defaultLocale=\"ko\">\n"
            "  <locale android:name=\"en-US\"/> text <![CDATA[ <cdata> ]]>\n"
            "  <locale android:name=\"ko\"/>\n"
            "</locale-config>");
  const std::filesystem::path file = dir.path() / "locales_config.xml";
  std::optional<LocaleConfig> parsed;
  const long all = allocationsOf([&] { parsed = parseLocaleConfig(file); });
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->supported.size(), 2u);
  REQUIRE_COUNTED(all);
  long threw = 0;
  for (long failAt = 0; failAt < all; failAt++) {
    SCOPED_TRACE(failAt);
    try {
      allocationsOf([&] { parseLocaleConfig(file); }, failAt);
    } catch (const std::bad_alloc&) {
      threw++;
    }
  }
  // parseLocaleConfig recovers from none.
  EXPECT_EQ(threw, all);
#endif
}

// What pugixml has no memory for as expat reads a file, which it says by an empty node or false,
// throws std::bad_alloc out of parseLocaleConfig too (#263), rather than give what it read of a
// tree with an element or an attribute left out. A long attribute and text take more than one of
// pugixml's pages. Not for a DLL, whose pugixml is not this program's.
TEST(ResourcesAllocations, PugixmlWithoutMemoryAsXmlIsReadThrows) {
  TempDir dir;
  const std::string filler(40000, 'x');
  dir.write("locales_config.xml",
            "<locale-config xmlns:android=\"http://schemas.android.com/apk/res/android\" "
            "android:defaultLocale=\"ko\" note=\"" +
                filler + "\">\n  <locale android:name=\"en-US\"/> " + filler +
                " <![CDATA[ <cdata> ]]>\n  <locale android:name=\"ko\"/>\n</locale-config>");
  const std::filesystem::path file = dir.path() / "locales_config.xml";
  std::optional<LocaleConfig> parsed;
  const long all = pugiAllocationsOf([&] { parsed = parseLocaleConfig(file); });
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->supported.size(), 2u);
  REQUIRE_COUNTED(all);
  for (long failAt = 0; failAt < all; failAt++) {
    SCOPED_TRACE(failAt);
    EXPECT_THROW(pugiAllocationsOf([&] { parseLocaleConfig(file); }, failAt), std::bad_alloc);
  }
}

// And addPackage does not take such a tree as the file (#263): a layout whose @+id are read out of
// its attributes fails, rather than load without them. libstdc++ lists the folder with operator
// new, which pugixml's allocations leave alone (#262).
TEST(ResourcesAllocations, PugixmlWithoutMemoryFailsAddPackage) {
  TempDir dir;
  const std::string filler(40000, 'x');
  dir.write("res/values/strings.xml",
            "<resources><string name=\"s\">" + filler + "</string></resources>");
  dir.write("res/layout/main.xml",
            "<LinearLayout xmlns:android=\"http://schemas.android.com/apk/res/android\" "
            "android:id=\"@+id/root\" android:tag=\"" +
                filler + "\">\n  <TextView android:id=\"@+id/text\"/>\n</LinearLayout>");
  const std::filesystem::path res = dir.path() / "res";
  long all = 0;
  {
    ResourceTable table;
    bool added = false;
    all = pugiAllocationsOf([&] { added = table.addPackage(res, "com.example", 0x7f); });
    ASSERT_TRUE(added);
    ASSERT_TRUE(table.find("com.example:id/text"));
  }
  REQUIRE_COUNTED(all);
  for (long failAt = 0; failAt < all; failAt++) {
    SCOPED_TRACE(failAt);
    ResourceTable table;
    bool added = false;
    try {
      pugiAllocationsOf([&] { added = table.addPackage(res, "com.example", 0x7f); }, failAt);
    } catch (const std::bad_alloc&) {
    }
    EXPECT_FALSE(added);
    EXPECT_TRUE(table.packages().empty());
  }
}

// The C API (#203), for each allocation its calls make but those of loading a package (#262),
// failing it: no exception leaves a C function, and one that fails says so and leaves what it would
// have written as it was. The table changes before each call that reads the configuration, so that
// each takes the change in.
TEST(ResourcesAllocations, NoExceptionLeavesTheCApi) {
#if HYDROID_NOEXCEPT_ALLOCATES
  GTEST_SKIP() << "the checked iterators allocate where they cannot throw";
#else
  TempDir dir;
  const std::string app = writePackage(dir, {"night"}, "com.example").u8string();
  const std::string lib = writePackage(dir, {"ko"}, "com.example.lib").u8string();
  // Made here, as an allocation of the test that fails would throw from it.
  const std::vector<std::string> libs = {"com.example.lib0", "com.example.lib1", "com.example.lib2",
                                         "com.example.lib3"};
  const char* locales[] = {"fr-FR", "ko-KR"};
  hydroid_res_config device{};
  device.ui_mode_night = 2;

  int failures = 0;  // of the calls, in one run
  const auto run = [&] {
    failures = 0;
    hydroid_res_table* table = hydroid_res_table_create();
    if (!table) {
      failures++;
      return;
    }
    // Not while a package loads: an allocation that fails as libstdc++ lists the folder ends the
    // program on GCC (#262).
    const auto add = [&](const std::string& res, const char* name, uint8_t id) {
      counting = false;
      if (hydroid_res_table_add_package(table, res.c_str(), name, id) != 1) failures++;
      counting = true;
    };
    add(app, "com.example", 0x7f);
    hydroid_res_resources* resources = hydroid_res_resources_create(table);
    if (!resources) {
      failures++;
      hydroid_res_table_destroy(table);
      return;
    }
    if (hydroid_res_resources_set_configuration(resources, &device, locales, 2) != 1) failures++;
    const uint32_t where = hydroid_res_table_find(table, "com.example:string/where", nullptr);

    const auto change = [&](size_t n) {
      add(lib, libs[n].c_str(), static_cast<uint8_t>(0x70 + n));
    };
    change(0);
    hydroid_res_value value;
    value.kind = HYDROID_RES_VALUE_BOOL;
    uint32_t generation = 0xdead;
    if (hydroid_res_resources_value(resources, where, &value, &generation) == -1) {
      failures++;
      EXPECT_EQ(value.kind, HYDROID_RES_VALUE_BOOL);
      EXPECT_EQ(generation, 0xdeadu);
    }
    change(1);
    if (hydroid_res_resources_value_for_density(resources, where, 480, &value, nullptr) == -1) {
      failures++;
    }
    change(2);
    hydroid_res_config config;
    config.mcc = 0xdead;
    if (hydroid_res_resources_configuration(resources, &config) != 1) {
      failures++;
      EXPECT_EQ(config.mcc, 0xdead);
    }
    change(3);
    generation = 0xdead;
    if (hydroid_res_resources_generation(resources, &generation) != 1) {
      failures++;
      EXPECT_EQ(generation, 0xdeadu);
    }
    hydroid_res_resources_destroy(resources);
    hydroid_res_table_destroy(table);
  };

  const long all = allocationsOf(run);
  REQUIRE_COUNTED(all);
  EXPECT_EQ(failures, 0) << "a call fails with no allocation failing";
  int failed = 0;
  for (long failAt = 0; failAt < all; failAt++) {
    SCOPED_TRACE(failAt);
    EXPECT_NO_THROW(allocationsOf(run, failAt));
    failed += failures > 0;
  }
  // Each call allocates, and one of them fails with each allocation that does.
  EXPECT_GT(failed, 0);
#endif
}

}  // namespace
}  // namespace hydroid::res

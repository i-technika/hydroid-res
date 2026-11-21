// What Resources allocates, as the operator new of this program counts it (#240): that a call
// that changes only the device does not go through the table again (#206), and that one whose
// allocation fails leaves all as it was. A program of its own, as hydroid-res-tests keeps the
// operator new of the sanitizers, which checks that each delete is of a new.

#include <gtest/gtest.h>

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

namespace {

// Allocations are counted while `counting`, from 0, and the one of the count `failing` throws.
std::atomic<bool> counting{false};
std::atomic<long> allocations{0};
std::atomic<long> failing{-1};

}  // namespace

void* operator new(std::size_t size) {
  if (counting.load() && allocations.fetch_add(1) == failing.load()) throw std::bad_alloc();
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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

// The allocations of adding a package of a string in values/ and each values-<qualifiers>/ of
// `folders`, which are all the library's.
long addPackage(ResourceTable& table, const TempDir& dir, const std::vector<const char*>& folders,
                const std::string& name = "com.example", uint8_t id = kAppPackageId) {
  const std::filesystem::path res = dir.path() / name;
  std::vector<std::string> all = {"values"};
  for (const char* folder : folders) all.push_back(std::string("values-") + folder);
  for (const std::string& folder : all) {
    std::filesystem::create_directories(res / folder);
    std::ofstream(res / folder / "strings.xml", std::ios::binary)
        << "<resources><string name=\"where\">" << folder << "</string></resources>";
  }
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

// For each allocation of a setConfiguration that chooses a locale, a call in which it fails:
// what Resources answers is as before, and the same call again chooses.
TEST(ResourcesAllocations, AFailedChoiceLeavesAllAsItWas) {
#if HYDROID_NOEXCEPT_ALLOCATES
  GTEST_SKIP() << "the checked iterators allocate where they cannot throw";
#else
  TempDir dir;
  ResourceTable table;
  REQUIRE_COUNTED(addPackage(table, dir, {"ko"}));
  const std::vector<Config> frKo = tags({"fr-FR", "ko-KR"});
  for (long failAt = 0;; failAt++) {
    SCOPED_TRACE(failAt);
    Resources resources(table);
    // One locale, which is not chosen: the table's are not gathered yet.
    resources.setConfiguration(dev("night"), tags({"fr-FR"}));
    const Config before = resources.configuration();
    const uint32_t generation = resources.generation();

    bool threw = false;
    try {
      allocationsOf([&] { resources.setConfiguration(dev("night"), frKo); }, failAt);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    if (!threw) {
      EXPECT_GT(failAt, 0) << "the call allocates nothing";
      EXPECT_EQ(chosen(resources), "ko-rKR-night");
      break;
    }
    EXPECT_EQ(resources.configuration(), before);
    EXPECT_EQ(resources.generation(), generation);
    resources.setConfiguration(dev("night"), frKo);
    EXPECT_EQ(chosen(resources), "ko-rKR-night");
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
  const std::vector<Config> frKo = tags({"fr-FR", "ko-KR"});
  for (long failAt = 0;; failAt++) {
    SCOPED_TRACE(failAt);
    ResourceTable table;
    REQUIRE_COUNTED(addPackage(table, dir, {"night"}));
    Resources resources(table);
    resources.setConfiguration(dev("night"), frKo);
    EXPECT_EQ(chosen(resources), "fr-rFR-night");
    addPackage(table, dir, {"ko"}, "com.example.lib", 0x7e);

    bool threw = false;
    try {
      allocationsOf([&] { resources.generation(); }, failAt);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    EXPECT_EQ(chosen(resources), "ko-rKR-night");
    if (!threw) {
      EXPECT_GT(failAt, 0) << "taking in the table allocates nothing";
      break;
    }
  }
#endif
}

}  // namespace
}  // namespace hydroid::res

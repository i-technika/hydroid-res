// A decimal-comma locale for the tests that check hydroid ignores the host app's locale.

#pragma once

#include <gtest/gtest.h>

#include <clocale>
#include <locale>
#include <stdexcept>
#include <string>

namespace hydroid::res::test {

// Sets a locale whose decimal point is a comma, as a host app may, for both C (strtof, printf)
// and C++ (default-constructed streams), and puts both back when it goes.
class DecimalCommaLocale {
 public:
  DecimalCommaLocale() {
    // Spelled as glibc, macOS and the Windows CRT name them.
    for (const char* name :
         {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "de-DE", "fr_FR.UTF-8", "fr_FR", "fr-FR"}) {
      if (!std::setlocale(LC_ALL, name)) continue;
      try {
        std::locale::global(std::locale(name));
      } catch (const std::runtime_error&) {
        restore();
        continue;
      }
      if (*std::localeconv()->decimal_point == ',' &&
          std::use_facet<std::numpunct<char>>(std::locale()).decimal_point() == ',') {
        name_ = name;
        return;
      }
      restore();
    }
  }
  ~DecimalCommaLocale() { restore(); }
  DecimalCommaLocale(const DecimalCommaLocale&) = delete;
  DecimalCommaLocale& operator=(const DecimalCommaLocale&) = delete;

  // nullptr if no such locale is installed.
  const char* name() const { return name_; }

 private:
  void restore() {
    std::locale::global(oldCpp_);
    std::setlocale(LC_ALL, oldC_.c_str());
  }

  std::locale oldCpp_;  // default-constructed: a copy of the global locale
  std::string oldC_ = std::setlocale(LC_ALL, nullptr);
  const char* name_ = nullptr;
};

}  // namespace hydroid::res::test

// Skips the test if `locale` found no decimal-comma locale. CI turns the skip into a failure
// (HYDROID_REQUIRE_LOCALE_TESTS), so that losing the locale does not pass unseen.
#ifdef HYDROID_REQUIRE_LOCALE_TESTS
#define HYDROID_NO_DECIMAL_COMMA_ FAIL
#else
#define HYDROID_NO_DECIMAL_COMMA_ GTEST_SKIP
#endif
#define HYDROID_NEED_DECIMAL_COMMA(locale) \
  if ((locale).name()) {                   \
  } else                                   \
    HYDROID_NO_DECIMAL_COMMA_() << "no locale with a decimal comma is installed"

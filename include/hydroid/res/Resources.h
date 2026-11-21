#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "hydroid/res/Config.h"
#include "hydroid/res/Export.h"
#include "hydroid/res/ResId.h"
#include "hydroid/res/ResourceTable.h"

// The values of a table for one device: of the values a resource has, each for a configuration,
// the one Android picks, as its AssetManager2::FindEntry does with ResTable_config::match and
// isBetterThan (Config.h). golden/data holds what Android itself picked of the golden app's res/
// under some hundreds of device configurations, and GoldenMatchTest checks these against it.

namespace hydroid::res {

// The value of `entry` for `device`, or nullptr if none matches it: of the candidates that match,
// the first, in their order (ResourceTable.h), then each one that isBetterThan the one held. That
// is the order Android meets them in, in the table aapt2 writes, and it matters, as isBetterThan
// is not an order.
HYDROID_RES_API const Candidate* bestCandidate(const Entry& entry, const Config& device);

// A table and the configuration of the device it is read for.
//
// Every member may be called from any thread at any time, but not while the table adds a
// package. A candidate it returns is of the table: it stays what it is as the configuration
// changes, and goes as the table's own entries do, when the table adds a package.
class Resources {
 public:
  // For `table`, which must outlive this, and the default configuration, which only takes the
  // values without qualifiers, or with a version up to kEmulatedSdkVersion (Version.h).
  HYDROID_RES_API explicit Resources(const ResourceTable& table);
  HYDROID_RES_API ~Resources();
  HYDROID_RES_API Resources(Resources&& other) noexcept;
  HYDROID_RES_API Resources& operator=(Resources&& other) noexcept;

  // Sets the state of the device, as Android's ResourcesImpl::updateConfiguration takes it:
  //   - `locales`, if there are any, are the user's languages, the most wanted first, of which
  //     only the locale is read (parseLanguageTag, ConfigParser.h, makes one), and the locale of
  //     `device` is replaced by one of them: the first that the table has resources for, or that
  //     is an English, which Android takes every app to have without a language; or the very
  //     first if there is none, or only one locale. The table has resources for a locale when
  //     one of its values, outside the package android (kFrameworkPackageId), is of its
  //     language and script, the script being worked out as in matches (Config.h); of all the
  //     packages, if those have values of no language. Without any, the first locale it is. The
  //     choice is for the whole table and not for each resource: a resource that has no value
  //     for the chosen language takes its value without a language, not that of the user's next.
  //   - A language that has an old code is given that code, which is the one Android's resources
  //     go by: he is iw, id is in, yi is ji. So values-iw/ is Hebrew, and values-he/ is not
  //     picked for it.
  //   - A version of 0 is kEmulatedSdkVersion.
  // The generation goes up if that changes the configuration.
  HYDROID_RES_API void setConfiguration(const Config& device,
                                        const std::vector<Config>& locales = {});

  // The configuration the values are picked for: the device's as setConfiguration made it, with
  // the locale it chose.
  HYDROID_RES_API Config configuration() const;

  // A number that changes whenever what `candidate` returns may: when setConfiguration changes
  // the configuration, and when the table has added a package. What is derived from the values
  // can be kept by it.
  HYDROID_RES_API uint32_t generation() const;

  // The value of the resource `id` for the configuration, or nullptr if the table has no such
  // resource, or none of its values matches. The answers are kept until the generation changes.
  HYDROID_RES_API const Candidate* candidate(ResId id) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hydroid::res

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
// Every member may be called from any thread at any time, but not while the table changes. A
// candidate it returns is of the table: it stays what it is as the configuration changes, and
// goes as the table's own entries do, when the table changes: when it adds a package, or is
// assigned or moved from (its revision, ResourceTable.h).
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
  //   - A version of 0, with no minor version, is kEmulatedSdkVersion. A minor version alone is
  //     kept as it is.
  //   - The longer side of the screen in pixels is its width, so a phone held upright, of
  //     1080x2400, is 2400x1080 and takes values-2000x1000/, not values-800x480/.
  //   - navHidden is Any, as Android tells its resources nothing of it.
  // The generation goes up if that changes the configuration. Each Resources looks for the
  // locales of the table in all its values once for each revision of it, and chooses the locale
  // again only when `locales` or the table have changed, so a call that changes the rest of the
  // device, as a window is resized, costs little however many values the table has.
  HYDROID_RES_API void setConfiguration(const Config& device,
                                        const std::vector<Config>& locales = {});

  // The configuration the values are picked for: the device's as setConfiguration made it, with
  // the locale it chose.
  HYDROID_RES_API Config configuration() const;

  // A number that changes whenever what `candidate` returns may: when setConfiguration changes
  // the configuration, and when the table has changed. What is derived from the values can be
  // kept by it: by the one `candidate` gives with them, or by one read before them, which at
  // worst is older than they are, and has them thrown away at the next look. Not by one read
  // after them: setConfiguration may have been called in between, on another thread, and the
  // values of the configuration before would be kept as those of the new one.
  HYDROID_RES_API uint32_t generation() const;

  // The value of the resource `id` for the configuration, or nullptr if the table has no such
  // resource, or none of its values matches. The answers are kept until the generation changes.
  // `generation`, if given, is set to the generation the answer is of.
  HYDROID_RES_API const Candidate* candidate(ResId id, uint32_t* generation = nullptr) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hydroid::res

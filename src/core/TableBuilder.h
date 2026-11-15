// The resources of a res/ folder linked into a package, as aapt2 link does.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/Parsed.h"
#include "hydroid/res/ResourceTable.h"

namespace hydroid::res::detail {

// Merges `folder` into the package `name` with the id `id`: one value per configuration and
// product, the default product kept, ids given and every reference resolved, into this package
// or one of `deps`. nullopt if any of it fails, with the problems in `diag`.
std::optional<Package> buildPackage(ParsedFolder folder, const std::string& name, uint8_t id,
                                    const ResourceTable& deps, Diagnostics& diag);

}  // namespace hydroid::res::detail

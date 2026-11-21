// A res/ folder as `aapt2 compile --dir` reads it: each folder a type and configuration, each
// file a resource, or values.

#pragma once

#include <filesystem>
#include <string>

#include "core/Parsed.h"

namespace hydroid::res::detail {

// Reads every file of the res/ folder `res` of the package `package` into `out`, in the order of
// their folder/name as bytes, in which the golden files have aapt2 link merge them (#80).
// Problems go to `diag`, which leave `package` out of the names of resources (#172).
void scanResFolder(const std::filesystem::path& res, const std::string& package, ParsedFolder& out,
                   Diagnostics& diag);

}  // namespace hydroid::res::detail

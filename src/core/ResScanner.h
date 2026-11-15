// A res/ folder as `aapt2 compile --dir` reads it: each folder a type and configuration, each
// file a resource, or values.

#pragma once

#include <filesystem>

#include "core/Parsed.h"

namespace hydroid::res::detail {

// Reads every file of the res/ folder `res` into `out`, folders and files in the order of their
// names. Problems go to `diag`.
void scanResFolder(const std::filesystem::path& res, ParsedFolder& out, Diagnostics& diag);

}  // namespace hydroid::res::detail

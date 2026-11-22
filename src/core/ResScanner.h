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

// The bytes of the file `path` into `text`, as scanResFolder reads each, or why they cannot be
// had: "failed to open file" or "failed to read file".
const char* readFile(const std::filesystem::path& path, std::string* text);

}  // namespace hydroid::res::detail

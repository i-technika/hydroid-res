// Values XML as aapt2 compile reads it (ResourceParser), and the parts of it the scanner and the
// table builder share.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/Parsed.h"
#include "hydroid/res/Config.h"

namespace hydroid::res::detail {

// Reads `xml`, the text of the values file `file` in a folder of `config`, into `out`: its
// resources, the ids its @+id/ references make, the attributes its <declare-styleable>s define
// and the ids of the names of their <enum>s and <flag>s. Problems go to `diag`.
void parseValues(std::string_view xml, const std::filesystem::path& file, const Config& config,
                 ParsedFolder& out, Diagnostics& diag);

// A reference as aapt2's ResourceUtils reads one: @[+][*][package:]type/entry, or
// ?[package:][attr/]entry, with whitespace around. The package is as written, not resolved.
struct ParsedReference {
  ResName name;
  bool attr = false;    // ?
  bool create = false;  // @+id/, which also makes the id
};
std::optional<ParsedReference> parseReference(std::string_view text);

// The text of a string as aapt2's StringBuilder makes it of `raw`, with no tags: whitespace runs
// collapse to one space outside "quotes", which are dropped, and \-escapes are read. nullopt with
// `error` for an unescaped apostrophe, a bad \u or text that is not UTF-8.
std::optional<std::string> buildString(std::string_view raw, std::string* error);

}  // namespace hydroid::res::detail

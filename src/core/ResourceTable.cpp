#include "hydroid/res/ResourceTable.h"

#include <iterator>
#include <ostream>
#include <string>
#include <utility>

#include "core/Parsed.h"
#include "core/PathUtf8.h"
#include "core/ResScanner.h"
#include "core/TableBuilder.h"
#include "core/ValuesParser.h"

namespace hydroid::res {
namespace {

// By ResType, aapt2's names of the types.
constexpr std::string_view kTypeNames[] = {
    "anim",         "animator", "array", "attr",      "bool",       "color",
    "dimen",        "drawable", "font",  "fraction",  "id",         "integer",
    "interpolator", "layout",   "menu",  "mipmap",    "navigation", "plurals",
    "raw",          "string",   "style", "styleable", "transition", "xml",
};
static_assert(std::size(kTypeNames) == static_cast<size_t>(ResType::Xml) + 1,
              "every ResType has a name, and the last is Xml");

std::string key(std::string_view package, ResType type, std::string_view name) {
  std::string k(package);
  k += ':';
  k += toString(type);
  k += '/';
  k += name;
  return k;
}

}  // namespace

const char* toString(ResType type) {
  const auto i = static_cast<size_t>(type);
  return i < std::size(kTypeNames) ? kTypeNames[i].data() : "?";
}

std::optional<ResType> parseResType(std::string_view name) {
  for (size_t i = 0; i < std::size(kTypeNames); i++) {
    if (kTypeNames[i] == name) return static_cast<ResType>(i);
  }
  return std::nullopt;
}

std::ostream& operator<<(std::ostream& os, const Diagnostic& d) {
  if (!d.file.empty()) os << detail::utf8(d.file) << ':';
  if (d.line > 0) os << d.line << ':';
  if (!d.file.empty() || d.line > 0) os << ' ';
  return os << d.message;
}

bool ResourceTable::addPackage(const std::filesystem::path& res, std::string_view name, uint8_t id,
                               std::vector<Diagnostic>* errors) {
  detail::Diagnostics diag;
  if (name.empty() || id == 0) {
    diag.error({}, "a package needs a name and an id other than 0");
  } else if (package(name) || package(id)) {
    diag.error({}, "a package named " + std::string(name) + " or with the id " +
                       std::to_string(id) + " is already loaded");
  }
  std::optional<Package> loaded;
  if (diag.empty()) {
    detail::ParsedFolder folder;
    detail::scanResFolder(res, folder, diag);
    if (diag.empty()) {
      loaded = detail::buildPackage(std::move(folder), std::string(name), id, *this, diag);
    }
  }
  if (!loaded) {
    if (errors) errors->insert(errors->end(), diag.errors().begin(), diag.errors().end());
    return false;
  }
  for (const TypeTable& type : loaded->types) {
    for (const Entry& e : type.entries) {
      byName_[key(loaded->name, type.type, e.name)] = makeResId(id, type.id, e.id);
    }
  }
  packages_.push_back(std::move(*loaded));
  return true;
}

const Package* ResourceTable::package(uint8_t id) const {
  for (const Package& p : packages_) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

const Package* ResourceTable::package(std::string_view name) const {
  for (const Package& p : packages_) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

std::optional<ResId> ResourceTable::find(std::string_view package, ResType type,
                                         std::string_view name) const {
  const auto it = byName_.find(key(package, type, name));
  if (it == byName_.end()) return std::nullopt;
  return it->second;
}

std::optional<ResId> ResourceTable::find(std::string_view name,
                                         std::string_view defaultPackage) const {
  const std::string text =
      name.empty() || name[0] != '@' ? "@" + std::string(name) : std::string(name);
  const std::optional<detail::ParsedReference> ref = detail::parseReference(text);
  if (!ref || ref->create || !ref->name.otherType.empty()) return std::nullopt;
  return find(ref->name.package.empty() ? defaultPackage : ref->name.package, ref->name.type,
              ref->name.entry);
}

const Entry* ResourceTable::entry(ResId id) const {
  const Package* p = package(packageId(id));
  return p ? p->entry(id) : nullptr;
}

std::optional<std::string> ResourceTable::nameOf(ResId id) const {
  const Package* p = package(packageId(id));
  const Entry* e = p ? p->entry(id) : nullptr;
  if (!e) return std::nullopt;
  return key(p->name, p->typeWithId(typeId(id))->type, e->name);
}

}  // namespace hydroid::res

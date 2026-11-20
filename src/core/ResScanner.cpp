#include "core/ResScanner.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/PathUtf8.h"
#include "core/ValuesParser.h"
#include "core/Whitespace.h"
#include "core/Xml.h"
#include "hydroid/res/ConfigParser.h"

#ifdef _WIN32
#include <share.h>
#endif

namespace hydroid::res::detail {
namespace {

namespace fs = std::filesystem;

// The entries of `dir` that are not hidden and are of the kind asked, by UTF-8 name. On Windows,
// aapt2 lists names in the ANSI code page and opens each by that name read as UTF-8, so it does
// not find one whose name is not Unicode text, a lone surrogate (#111): a folder of res/ so named
// has nothing in it, and anything so named in a type folder, a folder too, is not found. (It
// does not find a name past the ANSI code page either, é.txt as e.txt, which hydroid reads, as
// aapt2 does on other systems.)
// A folder that cannot be opened or read to its end is an error (#85), and so is an entry of res/
// whose kind cannot be told, as a junction or link to a folder that denies it, so that a load
// never quietly lacks, say, values-fr/. A link to nothing is left out, as aapt2 leaves it, since
// nothing is behind it to lack. aapt2 37.0.0 on Windows leaves such a folder out without
// a word; its own message for a folder it fails to open is "failed to open directory", which
// hydroid's is, the same everywhere, as the system's may not be UTF-8 (Windows writes it in the
// ANSI code page). In a type folder, as in aapt2's FileCollection, only folders are left out:
// anything else, a link to nothing or one whose kind cannot be told too, is a file, which read()
// fails on (#86).
std::vector<std::pair<std::string, fs::path>> list(const fs::path& dir, bool directories,
                                                   Diagnostics& diag) {
  std::vector<std::pair<std::string, fs::path>> out;
  std::error_code ec;
  fs::directory_iterator it(dir, ec);
  if (ec) {
    diag.error({dir, 0}, "failed to open directory");
    return out;
  }
  // increment(ec) rather than a range-for, whose ++ throws filesystem_error out of addPackage.
  for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const fs::directory_entry& e = *it;
    std::string name = utf8(e.path().filename());
    if (name.empty() || name[0] == '.') continue;  // file::IsHidden
    if (!isUnicode(e.path().filename())) {
      if (!directories) diag.error({e.path(), 0}, "file not found");
      continue;
    }
    std::error_code kind;
    const bool directory = e.is_directory(kind);
    if (directories && !directory) {
      if (kind && kind != std::errc::no_such_file_or_directory) {
        diag.error({e.path(), 0}, "failed to open directory");
      }
      continue;
    }
    if (!directories && directory) continue;
    out.emplace_back(std::move(name), e.path());
  }
  if (ec) diag.error({dir, 0}, "failed to read directory");
  std::sort(out.begin(), out.end());
  return out;
}

// The bytes of `path` into `text`, or why they cannot be had: aapt2's "failed to open file" for a
// file that cannot be opened, a link to nothing among them, and for what is not a file once links
// are followed, as a pipe, which aapt2 would open and wait on until something wrote to it (#86).
// It reads by stdio, whose ferror() tells a failed read from the end of the file: through an
// ifstream, MSVC's ends a failed read as if the file ended there, and libstdc++'s throws.
const char* read(const fs::path& path, std::string* text) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return "failed to open file";
#ifdef _WIN32
  // Shared for reading and writing, as fopen() opens it; _wfopen_s() would not share it.
  std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(_wfsopen(path.c_str(), L"rb", _SH_DENYNO),
                                                    std::fclose);
#else
  std::unique_ptr<std::FILE, int (*)(std::FILE*)> f(std::fopen(path.c_str(), "rb"), std::fclose);
#endif
  if (!f) return "failed to open file";
  text->clear();
  char buffer[16384];
  while (const size_t n = std::fread(buffer, 1, sizeof buffer, f.get())) text->append(buffer, n);
  return std::ferror(f.get()) ? "failed to read file" : nullptr;
}

// ExtractResourcePathData: the resource name is the file name up to .9.png, else up to its last
// '.', and its extension what follows.
void splitFileName(const std::string& file, std::string* name, std::string* extension) {
  constexpr std::string_view kNinePatch = ".9.png";
  if (file.size() > kNinePatch.size() &&
      file.compare(file.size() - kNinePatch.size(), kNinePatch.size(), kNinePatch) == 0) {
    *name = file.substr(0, file.size() - kNinePatch.size());
    *extension = "9.png";
  } else if (const size_t dot = file.rfind('.'); dot != std::string::npos) {
    *name = file.substr(0, dot);
    *extension = file.substr(dot + 1);
  } else {
    *name = file;
    extension->clear();
  }
}

// What the attributes of an XML file give the table builder: the ids that @+id/ in one makes, as
// aapt2's XmlIdCollector collects them, and each attribute, for the table builder to link as
// aapt2 link's XmlReferenceLinker does (#132), but one of tools:, which link leaves alone though
// its ids are made. Text is not looked at.
class XmlFileCollector {
 public:
  XmlFileCollector(const XmlDocument& doc, const fs::path& file, ParsedFolder& out,
                   Diagnostics& diag)
      : doc_(doc), file_(file), out_(out), diag_(diag) {}

  void visit(pugi::xml_node element) {
    if (elementNamespace(element) == kAaptNamespace && localName(element.name()) == "attr") {
      diag_.error({file_, doc_.line(element)}, "inline <aapt:attr> is not supported yet");
      return;
    }
    for (pugi::xml_attribute a : element.attributes()) {
      if (isNamespaceDeclaration(a)) continue;
      const Source source{file_, doc_.line(element)};
      const std::optional<ParsedReference> ref = parseReference(a.value());
      if (ref && ref->create) collectId(ref->name, source);
      const std::string ns = attributeNamespace(element, a);
      if (ns == kToolsNamespace) continue;
      ParsedXmlAttribute attribute;
      if (std::optional<std::string> package = namespacePackage(ns)) {
        attribute.attr =
            ResName{std::move(*package), ResType::Attr, std::string(localName(a.name()))};
      }
      // ResourceUtils::TryParseItemForAttribute reads @null, @empty and references first, as
      // any attribute takes them.
      ParsedItem& value = attribute.value;
      value.source = source;
      const std::string_view text = trim(a.value());
      if (text == "@null" || text == "@empty") {
        value.literal = text == "@null" ? Value::null() : Value::empty();
      } else if (ref) {
        value.kind = ref->attr ? ParsedItem::Kind::AttrRef : ParsedItem::Kind::Reference;
        value.ref = ref->name;
        value.ref.package = resolvePackage(element, value.ref.package);
      } else {
        value.kind = ParsedItem::Kind::Raw;
        value.text = a.value();
      }
      out_.xmlAttributes.push_back(std::move(attribute));
    }
    for (pugi::xml_node child : element.children()) {
      if (child.type() == pugi::node_element) visit(child);
    }
  }

 private:
  void collectId(const ResName& name, const Source& source) {
    if (!isValidEntryName(name.entry)) {
      diag_.error(source, "id '" + name.entry + "' has an invalid entry name");
      return;
    }
    // The package as written, which the table builder checks (#83): aapt2 does not read it as an
    // XML namespace prefix here, though link reads the reference so.
    if (!seen_.insert(name.package + ":" + name.entry).second) return;
    out_.resources.push_back(makeIdResource(name.entry, source));
    out_.resources.back().package = name.package;
    out_.resources.back().inXmlFile = true;
  }

  const XmlDocument& doc_;
  const fs::path& file_;
  ParsedFolder& out_;
  Diagnostics& diag_;
  std::set<std::string> seen_;
};

}  // namespace

void scanResFolder(const fs::path& res, ParsedFolder& out, Diagnostics& diag) {
  std::error_code ec;
  if (!fs::is_directory(res, ec)) {
    diag.error({res, 0}, "not a folder");
    return;
  }
  // The files in the order of their folder/name as bytes, the order in which the golden files have
  // aapt2 link merge the .flat files (#80): res/values-land/a.xml ('-' is 0x2D) before
  // res/values/a.xml ('/' is 0x2F), not each folder's files after the folder before it. Link keeps
  // the first of two values that agree, so the order shows. aapt2 has no one order of its own:
  // compile --dir keeps the files by their native path, where Windows's '\' (0x5C) puts
  // values-v21/ before values-v2/, and link merges the zip compile writes in an order that
  // changes with the names in it.
  struct File {
    std::string path;  // folder/name, by which the files are read
    std::string dirName;
    std::string fileName;
    fs::path file;
  };
  std::vector<File> files;
  for (const auto& [dirName, dir] : list(res, /*directories=*/true, diag)) {
    for (auto& [fileName, file] : list(dir, /*directories=*/false, diag)) {
      files.push_back({dirName + "/" + fileName, dirName, std::move(fileName), std::move(file)});
    }
  }
  std::sort(files.begin(), files.end(),
            [](const File& a, const File& b) { return a.path < b.path; });
  std::map<std::string, std::string> flats;  // compiled name -> folder/name
  for (const auto& [path, dirName, fileName, file] : files) {
    const std::optional<ResDir> resDir = parseResDir(dirName);
    const bool values = resDir && resDir->type == "values";
    const std::optional<ResType> type = resDir ? parseResType(resDir->type) : std::nullopt;
    if (!resDir) {
      diag.error({file, 0}, "invalid configuration in folder name '" + dirName + "'");
      continue;
    }
    std::string name;
    std::string extension;
    splitFileName(fileName, &name, &extension);
    if (!values && !type) {
      diag.error({file, 0}, "invalid file path: no resource type '" + resDir->type + "'");
      continue;
    }
    if (values && extension != "xml") {
      diag.error({file, 0}, "invalid file path: not an .xml file in " + dirName);
      continue;
    }
    // aapt2 compiles everything in xml/ as XML, and .xml files elsewhere but in raw/.
    const bool xml =
        !values && *type != ResType::Raw && (*type == ResType::Xml || extension == "xml");
    const bool png =
        !values && *type != ResType::Raw && (extension == "png" || extension == "9.png");
    // aapt2 reserves '.' in the name of a file it compiles, as values, XML or PNG, though not of
    // one it copies as it is, as raw/a.b.txt or drawable/a.b.jpg (#121).
    if ((values || xml || png) && name.find('.') != std::string::npos) {
      diag.error({file, 0}, "file name cannot contain '.' other than for specifying the extension");
      continue;
    }
    // The name aapt2 compile gives the file: the folder's name, less a '-' nothing follows, '_'
    // and the file's name, with .arsc for the .xml of a values file, then .flat. aapt2 link
    // fails on two files of one name, "Duplicate entries in archive", as values-/a.xml and
    // values/a.xml (#122).
    const size_t dash = dirName.find('-');
    std::string flat =
        dash != std::string::npos && dash + 1 == dirName.size() ? dirName.substr(0, dash) : dirName;
    flat += "_" + (values ? name + ".arsc" : fileName) + ".flat";
    if (const auto [it, added] = flats.emplace(flat, path); !added) {
      diag.error({file, 0}, "compiles to " + flat + ", as " + it->second + " does");
      continue;
    }
    std::string text;
    if (const char* why = read(file, &text)) {
      diag.error({file, 0}, why);
      continue;
    }
    if (values) {
      parseValues(text, file, resDir->config, out, diag);
      continue;
    }

    // The path aapt2 link gives it: the name, then what follows the file name's first '.'.
    FileRef ref;
    ref.path = "res/" + std::string(toString(*type));
    if (resDir->config != Config{}) ref.path += "-" + toString(resDir->config);
    ref.path += "/" + name;
    if (const size_t dot = fileName.find('.'); dot != std::string::npos) {
      ref.path += fileName.substr(dot);
    }
    if (xml) {
      ref.type = FileType::Xml;
      XmlDocument doc;
      std::string why;
      int line = 0;
      if (!doc.load(text, XmlExtent::Document, &why, &line)) {
        diag.error({file, line}, "xml parser error: " + why);
        continue;
      }
      XmlFileCollector(doc, file, out, diag).visit(doc.root());
    } else if (png) {
      ref.type = FileType::Png;
    }
    ParsedResource resource;
    resource.type = *type;
    resource.name = name;
    resource.config = resDir->config;
    resource.value = std::move(ref);
    resource.source = {file, 0};
    out.resources.push_back(std::move(resource));
  }
}

}  // namespace hydroid::res::detail

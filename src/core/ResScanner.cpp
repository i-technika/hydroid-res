#include "core/ResScanner.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/PathUtf8.h"
#include "core/ValuesParser.h"
#include "core/Xml.h"
#include "hydroid/res/ConfigParser.h"

namespace hydroid::res::detail {
namespace {

namespace fs = std::filesystem;

// The entries of `dir` that are not hidden and are of the kind asked, by UTF-8 name. On Windows,
// aapt2 lists names in the ANSI code page and opens each by that name read as UTF-8, so it does
// not find one whose name is not Unicode text, a lone surrogate (#111): a folder of res/ so named
// has nothing in it, and anything so named in a type folder, a folder too, is not found. (It
// does not find a name past the ANSI code page either, é.txt as e.txt, which hydroid reads, as
// aapt2 does on other systems.)
std::vector<std::pair<std::string, fs::path>> list(const fs::path& dir, bool directories,
                                                   Diagnostics& diag) {
  std::vector<std::pair<std::string, fs::path>> out;
  std::error_code ec;
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    std::string name = utf8(e.path().filename());
    if (name.empty() || name[0] == '.') continue;  // file::IsHidden
    if (!isUnicode(e.path().filename())) {
      if (!directories) diag.error({e.path(), 0}, "file not found");
      continue;
    }
    if (directories ? !e.is_directory(ec) : !e.is_regular_file(ec)) continue;
    out.emplace_back(std::move(name), e.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool read(const fs::path& path, std::string* text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  text->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
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

// aapt2's XmlIdCollector: the ids that @+id/ in an attribute of an XML file makes. Text is not
// looked at.
class IdCollector {
 public:
  IdCollector(const XmlDocument& doc, const fs::path& file, ParsedFolder& out, Diagnostics& diag)
      : doc_(doc), file_(file), out_(out), diag_(diag) {}

  void visit(pugi::xml_node element) {
    const std::optional<std::string> ns = elementNamespace(element);
    if (!ns) {
      diag_.error({file_, doc_.line(element)},
                  "unbound prefix of <" + std::string(element.name()) + ">");
      return;
    }
    if (*ns == kAaptNamespace && localName(element.name()) == "attr") {
      diag_.error({file_, doc_.line(element)}, "inline <aapt:attr> is not supported yet");
      return;
    }
    for (pugi::xml_attribute a : element.attributes()) {
      if (isNamespaceDeclaration(a)) continue;
      const std::optional<ParsedReference> ref = parseReference(a.value());
      if (!ref || !ref->create || !ref->name.package.empty()) continue;
      const Source source{file_, doc_.line(element)};
      if (!isValidEntryName(ref->name.entry)) {
        diag_.error(source, "id '" + ref->name.entry + "' has an invalid entry name");
        continue;
      }
      if (!seen_.insert(ref->name.entry).second) continue;
      ParsedResource id;
      id.type = ResType::Id;
      id.name = ref->name.entry;
      ParsedItem item;
      item.kind = ParsedItem::Kind::Id;
      item.source = source;
      id.value = std::move(item);
      id.source = source;
      out_.resources.push_back(std::move(id));
    }
    for (pugi::xml_node child : element.children()) {
      if (child.type() == pugi::node_element) visit(child);
    }
  }

 private:
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
  for (const auto& [dirName, dir] : list(res, /*directories=*/true, diag)) {
    const std::optional<ResDir> resDir = parseResDir(dirName);
    const bool values = resDir && resDir->type == "values";
    const std::optional<ResType> type = resDir ? parseResType(resDir->type) : std::nullopt;
    for (const auto& [fileName, file] : list(dir, /*directories=*/false, diag)) {
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
      std::string text;
      if (!read(file, &text)) {
        diag.error({file, 0}, "cannot read the file");
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
      // aapt2 compiles everything in xml/ as XML, and .xml files elsewhere but in raw/.
      if (*type != ResType::Raw && (*type == ResType::Xml || extension == "xml")) {
        ref.type = FileType::Xml;
        XmlDocument doc;
        std::string why;
        int line = 0;
        if (!doc.load(text, XmlExtent::Document, &why, &line)) {
          diag.error({file, line}, "xml parser error: " + why);
          continue;
        }
        IdCollector(doc, file, out, diag).visit(doc.root());
      } else if (*type != ResType::Raw && (extension == "png" || extension == "9.png")) {
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
}

}  // namespace hydroid::res::detail

// `aapt2 dump resources`, as aapt2's Debug::PrintTable and its value printers write it.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include "core/Parsed.h"
#include "hydroid/res/Complex.h"
#include "hydroid/res/ResourceTable.h"
#include "hydroid/res/ValueParser.h"

namespace hydroid::res {
namespace {

// text::Printer: indents each line it starts with something on it, two spaces a level, newlines
// inside a string included.
class Printer {
 public:
  explicit Printer(std::ostream& os) : os_(os) {}

  Printer& print(std::string_view s) {
    while (!s.empty()) {
      const size_t newline = s.find('\n');
      const std::string_view line = s.substr(0, newline);
      if (!line.empty()) {
        if (needsIndent_) {
          for (int i = 0; i < indent_; i++) os_ << "  ";
          needsIndent_ = false;
        }
        os_ << line;
      }
      if (newline == std::string_view::npos) break;
      os_ << '\n';
      needsIndent_ = true;
      s.remove_prefix(newline + 1);
    }
    return *this;
  }
  Printer& println(std::string_view s = {}) { return print(s).print("\n"); }
  void indent() { indent_++; }
  void undent() { indent_--; }

 private:
  std::ostream& os_;
  int indent_ = 0;
  bool needsIndent_ = true;
};

// printf's %f, %e and %g with 6 digits, in the C locale whatever the global one is.
std::string formatDouble(double d, std::ios_base::fmtflags floatfield) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.setf(floatfield, std::ios_base::floatfield);
  out << std::setprecision(6) << d;
  return out.str();
}

std::string hex8(uint32_t x) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08x", static_cast<unsigned>(x));
  return buf;
}

std::string hex2(uint32_t x) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02x", static_cast<unsigned>(x));
  return buf;
}

// How aapt2 37.0.0 prints a float: %e under 1e-10 and past 2^63 in magnitude, 0 included; an
// integer as one where an int64_t holds it; %g otherwise.
std::string floatText(float f) {
  const double d = f;
  constexpr double kTwo63 = 9223372036854775808.0;
  if (std::fabs(f) < 1e-10f || std::fabs(d) > kTwo63) {
    return formatDouble(d, std::ios_base::scientific);
  }
  if (d >= -kTwo63 && d < kTwo63 && d == std::trunc(d)) {
    return std::to_string(static_cast<int64_t>(d));
  }
  return formatDouble(d, std::ios_base::fmtflags{});
}

// libandroidfw's complex_value, which the dump prints: unlike TypedValue.complexToFloat it reads
// the mantissa unsigned, so -1.5dp prints as 510.500000dp.
float dumpedComplex(uint32_t data) {
  constexpr float kMultipliers[] = {1.0f / (1 << 8), 1.0f / (1 << 7) / (1 << 8),
                                    1.0f / (1 << 15) / (1 << 8), 1.0f / (1 << 23) / (1 << 8)};
  return static_cast<float>(data & (kComplexMantissaMask << kComplexMantissaShift)) *
         kMultipliers[(data >> kComplexRadixShift) & kComplexRadixMask];
}

class ValuePrinter {
 public:
  // `type` is that of the entry the value is of.
  ValuePrinter(Printer& p, const Package& package, ResType type)
      : p_(p), package_(package), type_(type) {}

  // What follows the configuration, on its line.
  void headline(const Value& v) {
    switch (v.kind()) {
      case ValueKind::Style: {
        const Style& style = package_.styles[v.index()];
        p_.print("(style) size=" + std::to_string(style.items.size()));
        if (style.parent != 0) {
          p_.print(" parent=");
          if (const Entry* e = ownEntry(style.parent)) {
            p_.print("style/" + e->name + " (" + hex8(style.parent) + ")");
          } else {
            p_.print(hex8(style.parent));
          }
        }
        break;
      }
      case ValueKind::Array:
        p_.print("(array) size=" + std::to_string(package_.arrays[v.index()].items.size()));
        break;
      case ValueKind::Plurals: {
        size_t n = 0;
        for (const auto& slot : package_.plurals[v.index()].items) n += slot.has_value();
        p_.print("(plurals) size=" + std::to_string(n));
        break;
      }
      case ValueKind::AttrDecl: {
        const AttrDecl& attr = package_.attrs[v.index()];
        p_.print("(attr) type=" + detail::formatsToString(attr.formats));
        if (!attr.symbols.empty()) p_.print(" size=" + std::to_string(attr.symbols.size()));
        break;
      }
      case ValueKind::StyleableDecl:
        p_.print("(styleable) size=" + std::to_string(package_.styleables[v.index()].attrs.size()));
        break;
      default: item(v); break;
    }
  }

  // The lines under it, for a bag.
  void body(const Value& v) {
    switch (v.kind()) {
      case ValueKind::Style:
        for (const Style::Item& i : package_.styles[v.index()].items) {
          if (const Entry* e = ownEntry(i.attr)) {
            p_.print(e->name + "(" + hex8(i.attr) + ")");
          } else {
            p_.print(hex8(i.attr));
          }
          p_.print("=");
          item(i.value);
          p_.println();
        }
        break;
      case ValueKind::Array: {
        const std::vector<Value>& items = package_.arrays[v.index()].items;
        p_.print("[");
        for (size_t i = 0; i < items.size(); i++) {
          if (i != 0 && i % 4 == 0) p_.println().print(" ");
          item(items[i]);
          if (i + 1 != items.size()) p_.print(", ");
        }
        p_.println("]");
        break;
      }
      case ValueKind::Plurals: {
        static constexpr const char* kNames[] = {"zero", "one", "two", "few", "many", "other"};
        const Plurals& plurals = package_.plurals[v.index()];
        for (size_t i = 0; i < plurals.items.size(); i++) {
          if (!plurals.items[i]) continue;
          p_.print(std::string(kNames[i]) + "=");
          item(*plurals.items[i]);
          p_.println();
        }
        break;
      }
      case ValueKind::AttrDecl: {
        const AttrDecl& attr = package_.attrs[v.index()];
        if (!(attr.formats & (kFormatEnum | kFormatFlags))) break;
        for (const AttrDecl::Symbol& s : attr.symbols) {
          if (const Entry* e = ownEntry(s.id)) {
            p_.print(e->name + "(" + hex8(s.id) + ")");
          } else {
            p_.print(hex8(s.id));
          }
          p_.println("=" + hex8(s.value));
        }
        break;
      }
      case ValueKind::StyleableDecl:
        for (ResId attr : package_.styleables[v.index()].attrs) {
          if (const Entry* e = ownEntry(attr)) p_.print(e->name + "(" + hex8(attr) + ")");
          p_.println(hex8(attr));
        }
        break;
      default: break;
    }
  }

 private:
  // The entry of `id` if it is of this package: the dump of an APK knows the names of no others.
  const Entry* ownEntry(ResId id) const {
    return packageId(id) == package_.id ? package_.entry(id) : nullptr;
  }

  std::string refName(ResId id) const {
    if (const Entry* e = ownEntry(id)) {
      return std::string(toString(package_.types[typeId(id) - 1].type)) + "/" + e->name;
    }
    return hex8(id);
  }

  // A file as aapt2 reads it back from an APK: XML or PNG by the end of its path, whatever it
  // compiled, and neither in raw/.
  void file(const std::string& path) {
    p_.print("(file) " + path);
    if (type_ == ResType::Raw) return;
    const auto endsWith = [&](std::string_view end) {
      return path.size() >= end.size() &&
             path.compare(path.size() - end.size(), end.size(), end) == 0;
    };
    if (endsWith(".xml")) {
      p_.print(" type=XML");
    } else if (endsWith(".png")) {
      p_.print(" type=PNG");
    }
  }

  // A value that fits a Res_value, as Item::PrettyPrint writes it.
  void item(const Value& v) {
    switch (v.kind()) {
      case ValueKind::Null: p_.print("@null"); break;
      case ValueKind::Empty: p_.print("@empty"); break;
      case ValueKind::Id: p_.print("(id)"); break;
      case ValueKind::Reference: p_.print("@" + refName(v.ref())); break;
      case ValueKind::AttrRef: p_.print("?" + refName(v.ref())); break;
      case ValueKind::String: {
        // aapt2 reads a string that starts with res/ back from an APK as a file, but in a string.
        const std::string& s = package_.strings[v.index()];
        if (type_ != ResType::String && s.rfind("res/", 0) == 0) {
          file(s);
        } else {
          p_.print("\"" + s + "\"");
        }
        break;
      }
      case ValueKind::StyledString: {
        const StyledString& s = package_.styledStrings[v.index()];
        p_.print("(styled string) \"" + s.text + "\"");
        for (const Span& span : s.spans) {
          p_.print(" " + span.name + ":" + std::to_string(span.firstChar) + "," +
                   std::to_string(span.lastChar));
        }
        break;
      }
      case ValueKind::Color: {
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%08x", static_cast<unsigned>(v.argb()));
        p_.print(buf);
        break;
      }
      case ValueKind::Int:
        p_.print(v.intFormat() == IntFormat::Hex ? hex8(static_cast<uint32_t>(v.intValue()))
                                                 : std::to_string(v.intValue()));
        break;
      case ValueKind::Bool: p_.print(v.boolValue() ? "true" : "false"); break;
      case ValueKind::Float: p_.print(floatText(v.floatValue())); break;
      case ValueKind::Dimension: {
        static constexpr const char* kUnits[] = {"px", "dp", "sp", "pt", "in", "mm"};
        const uint32_t unit = complexUnit(v.complexData());
        p_.print(formatDouble(dumpedComplex(v.complexData()), std::ios_base::fixed) +
                 (unit < 6 ? kUnits[unit] : "???"));
        break;
      }
      case ValueKind::Fraction: {
        const uint32_t unit = complexUnit(v.complexData());
        p_.print(formatDouble(dumpedComplex(v.complexData()), std::ios_base::fixed) +
                 (unit == kComplexUnitFraction         ? "%"
                  : unit == kComplexUnitFractionParent ? "%p"
                                                       : "???"));
        break;
      }
      case ValueKind::File: file(package_.files[v.index()].path); break;
      case ValueKind::Style:
      case ValueKind::Array:
      case ValueKind::Plurals:
      case ValueKind::AttrDecl:
      case ValueKind::StyleableDecl: headline(v); break;
    }
  }

  Printer& p_;
  const Package& package_;
  ResType type_;
};

}  // namespace

namespace detail {

std::string formatsToString(uint32_t formats) {
  if (formats == kFormatAny) return "any";
  static constexpr std::pair<uint32_t, const char*> kNames[] = {
      {kFormatReference, "reference"}, {kFormatString, "string"},     {kFormatInteger, "integer"},
      {kFormatBoolean, "boolean"},     {kFormatColor, "color"},       {kFormatFloat, "float"},
      {kFormatDimension, "dimension"}, {kFormatFraction, "fraction"}, {kFormatEnum, "enum"},
      {kFormatFlags, "flags"},
  };
  std::string out;
  for (const auto& [bit, name] : kNames) {
    if (!(formats & bit)) continue;
    if (!out.empty()) out += '|';
    out += name;
  }
  return out;
}

}  // namespace detail

void dump(std::ostream& os, const ResourceTable& table) {
  Printer p(os);
  for (const Package& package : table.packages()) {
    // aapt2 writes no package of which an APK holds nothing: none, or only styleables.
    bool holdsAny = false;
    for (const TypeTable& type : package.types) holdsAny |= type.type != ResType::Styleable;
    if (!holdsAny) continue;
    p.println("Package name=" + package.name + " id=" + hex2(package.id));
    p.indent();
    for (const TypeTable& type : package.types) {
      if (type.type == ResType::Styleable) continue;  // not in an APK's resources.arsc
      p.println("type " + std::string(toString(type.type)) + " id=" + hex2(type.id) +
                " entryCount=" + std::to_string(type.entries.size()));
      p.indent();
      for (size_t e = 0; e < type.entries.size(); e++) {
        const Entry& entry = type.entries[e];
        const ResId id = makeResId(package.id, type.id, static_cast<uint16_t>(e));
        p.println("resource " + hex8(id) + " " + toString(type.type) + "/" + entry.name +
                  (entry.isPublic ? " PUBLIC" : ""));
        p.indent();
        for (const Candidate& c : entry.candidates) {
          ValuePrinter values(p, package, type.type);
          p.print("(" + toString(c.config) + ") ");
          values.headline(c.value);
          p.println();
          p.indent();
          values.body(c.value);
          p.undent();
        }
        p.undent();
      }
      p.undent();
    }
    p.undent();
  }
}

std::string describe(const Package& package, ResType type, const Value& value) {
  std::ostringstream out;
  Printer p(out);
  ValuePrinter values(p, package, type);
  values.headline(value);
  p.println();
  values.body(value);
  std::string s = out.str();
  s.pop_back();  // the newline after the last line
  return s;
}

}  // namespace hydroid::res

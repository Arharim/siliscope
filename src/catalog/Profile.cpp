#include "siliscope/Profile.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct RuleMeta {
  std::string severity;
  std::string def; // "on" | "off" | "advisory"
};

struct ProfileAst {
  std::string extends;
  bool from_default = false;
  std::vector<std::string> extra;
  std::vector<std::string> exclude;
  std::vector<std::pair<std::string, std::string>> overrides;
};

using Catalog = std::unordered_map<std::string, RuleMeta>;

static llvm::StringRef stripComment(llvm::StringRef s) {
  const size_t h = s.find('#');
  if (h != llvm::StringRef::npos) {
    s = s.take_front(h);
  }
  return s.rtrim();
}

static std::string unquote(llvm::StringRef s) {
  s = s.trim();
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.drop_front().drop_back().str();
  }
  return s.str();
}

static bool parseBool(llvm::StringRef s) {
  s = s.trim();
  return s.equals_insensitive("true") || s.equals_insensitive("yes") ||
         s.equals_insensitive("on") || s.equals_insensitive("1");
}

static unsigned indentOf(llvm::StringRef raw) {
  unsigned n = 0;
  for (char c : raw) {
    if (c == ' ') {
      ++n;
    } else if (c == '\t') {
      n += 2;
    } else {
      break;
    }
  }
  return n;
}

static bool splitKey(llvm::StringRef line, llvm::StringRef &key, llvm::StringRef &val) {
  const size_t c = line.find(':');
  if (c == llvm::StringRef::npos) {
    return false;
  }
  key = line.take_front(c).trim();
  val = line.drop_front(c + 1).trim();
  return !key.empty();
}

static std::string dashItem(llvm::StringRef line) {
  line = line.trim();
  if (!line.starts_with("- ")) {
    return {};
  }
  return unquote(line.drop_front(2));
}

static bool readFile(const llvm::Twine &path, std::string &text, std::string &err) {
  auto mb = llvm::MemoryBuffer::getFile(path);
  if (!mb) {
    err = mb.getError().message();
    return false;
  }
  text = (*mb)->getBuffer().str();
  return true;
}

static std::vector<std::string> listYamlStems(llvm::StringRef dir) {
  std::vector<std::string> names;
  std::error_code ec;
  for (llvm::sys::fs::directory_iterator it(dir, ec), e; it != e && !ec; it.increment(ec)) {
    if (llvm::sys::path::extension(it->path()) != ".yaml") {
      continue;
    }
    names.emplace_back(llvm::sys::path::stem(it->path()).str());
  }
  std::sort(names.begin(), names.end());
  return names;
}

static bool loadCatalog(const std::string &ruleset_dir, Catalog &cat, std::string &err) {
  llvm::SmallString<256> dir(ruleset_dir);
  llvm::sys::path::append(dir, "rules");
  std::error_code ec;
  bool any = false;
  for (llvm::sys::fs::directory_iterator it(dir, ec), e; it != e && !ec; it.increment(ec)) {
    if (llvm::sys::path::extension(it->path()) != ".yaml") {
      continue;
    }
    std::string text;
    std::string file_err;
    if (!readFile(it->path(), text, file_err)) {
      err = std::string(it->path()) + ": " + file_err;
      return false;
    }
    any = true;
    std::string id;
    RuleMeta meta;
    auto flush = [&]() {
      if (id.empty()) {
        return;
      }
      if (meta.severity.empty()) {
        meta.severity = "warning";
      }
      if (meta.def.empty()) {
        meta.def = "off";
      }
      cat.emplace(id, meta);
      id.clear();
      meta = {};
    };
    llvm::StringRef rest(text);
    while (!rest.empty()) {
      llvm::StringRef raw;
      std::tie(raw, rest) = rest.split('\n');
      if (raw.ends_with('\r')) {
        raw = raw.drop_back();
      }
      const llvm::StringRef line = stripComment(raw);
      if (line.trim().empty()) {
        continue;
      }
      if (raw.starts_with("  - id:")) {
        flush();
        id = unquote(line.drop_front(llvm::StringRef("  - id:").size()));
        continue;
      }
      llvm::StringRef key, val;
      if (id.empty() || indentOf(raw) != 4 || !splitKey(line.trim(), key, val)) {
        continue;
      }
      if (key == "severity") {
        meta.severity = unquote(val);
      } else if (key == "default") {
        meta.def = unquote(val);
      }
    }
    flush();
  }
  if (ec) {
    err = "ruleset/rules: " + ec.message();
    return false;
  }
  if (!any || cat.empty()) {
    err = "no rules in " + std::string(dir);
    return false;
  }
  return true;
}

static bool loadProfileAst(const std::string &path, ProfileAst &ast, std::string &err) {
  std::string text;
  if (!readFile(path, text, err)) {
    return false;
  }
  enum class Sec { Top, Include, Extra, Exclude, Overrides, Skip };
  Sec sec = Sec::Top;
  llvm::StringRef rest(text);
  while (!rest.empty()) {
    llvm::StringRef raw;
    std::tie(raw, rest) = rest.split('\n');
    if (raw.ends_with('\r')) {
      raw = raw.drop_back();
    }
    const llvm::StringRef line = stripComment(raw);
    if (line.trim().empty()) {
      continue;
    }
    const unsigned ind = indentOf(raw);
    llvm::StringRef key, val;
    if (ind == 0 && splitKey(line, key, val)) {
      if (key == "extends") {
        ast.extends = unquote(val);
        sec = Sec::Top;
      } else if (key == "include") {
        sec = Sec::Include;
      } else if (key == "exclude") {
        sec = Sec::Exclude;
        if (val == "[]") {
          sec = Sec::Top;
        }
      } else if (key == "severity_overrides") {
        sec = Sec::Overrides;
      } else {
        sec = (key == "name" || key == "languages") ? Sec::Top : Sec::Skip;
      }
      continue;
    }
    if (sec == Sec::Include && splitKey(line.trim(), key, val)) {
      if (key == "from_default") {
        ast.from_default = parseBool(val);
      } else if (key == "extra") {
        sec = Sec::Extra;
      }
      continue;
    }
    if (sec == Sec::Extra) {
      std::string item = dashItem(line);
      if (!item.empty()) {
        ast.extra.push_back(std::move(item));
      }
      continue;
    }
    if (sec == Sec::Exclude) {
      std::string item = dashItem(line);
      if (!item.empty()) {
        ast.exclude.push_back(std::move(item));
      }
      continue;
    }
    if (sec == Sec::Overrides && splitKey(line.trim(), key, val)) {
      ast.overrides.emplace_back(key.str(), unquote(val));
    }
  }
  return true;
}

static bool knownRule(const Catalog &cat, const std::string &id, std::string &err) {
  if (cat.count(id)) {
    return true;
  }
  err = "unknown rule id " + id;
  return false;
}

static bool resolve(const std::string &ruleset_dir,
                    const std::string &name,
                    const Catalog &cat,
                    std::vector<std::string> &stack,
                    Profile &out,
                    std::string &err) {
  if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
    err = "profile cycle involving " + name;
    return false;
  }

  llvm::SmallString<256> path(ruleset_dir);
  llvm::sys::path::append(path, "profiles", name + ".yaml");
  if (!llvm::sys::fs::exists(path)) {
    llvm::SmallString<256> pdir(ruleset_dir);
    llvm::sys::path::append(pdir, "profiles");
    const auto have = listYamlStems(pdir);
    err = "unknown profile '" + name + "'";
    if (!have.empty()) {
      err += " (have ";
      for (size_t i = 0; i < have.size(); ++i) {
        if (i) {
          err += ", ";
        }
        err += have[i];
      }
      err += ")";
    }
    return false;
  }

  ProfileAst ast;
  std::string file_err;
  if (!loadProfileAst(std::string(path), ast, file_err)) {
    err = std::string(path) + ": " + file_err;
    return false;
  }

  stack.push_back(name);
  if (!ast.extends.empty()) {
    if (!resolve(ruleset_dir, ast.extends, cat, stack, out, err)) {
      return false;
    }
  }
  stack.pop_back();

  if (ast.from_default) {
    for (const auto &kv : cat) {
      if (kv.second.def == "on") {
        out.enabled[kv.first] = kv.second.severity;
      }
    }
  }
  for (const std::string &id : ast.extra) {
    if (!knownRule(cat, id, err)) {
      return false;
    }
    out.enabled[id] = cat.at(id).severity;
  }
  for (const std::string &id : ast.exclude) {
    if (!knownRule(cat, id, err)) {
      return false;
    }
    out.enabled.erase(id);
  }
  for (const auto &ov : ast.overrides) {
    if (!knownRule(cat, ov.first, err)) {
      return false;
    }
    auto it = out.enabled.find(ov.first);
    if (it != out.enabled.end()) {
      it->second = ov.second;
    }
  }
  return true;
}

} // namespace

bool loadProfile(const std::string &ruleset_dir,
                 const std::string &name,
                 Profile &out,
                 std::string &err) {
  std::string n = name;
  if (llvm::StringRef(n).ends_with(".yaml")) {
    n.resize(n.size() - 5);
  }
  Catalog cat;
  if (!loadCatalog(ruleset_dir, cat, err)) {
    return false;
  }
  out = {};
  out.name = n;
  std::vector<std::string> stack;
  return resolve(ruleset_dir, n, cat, stack, out, err);
}

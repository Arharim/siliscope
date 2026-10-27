#include "siliscope/Report.h"

#include "siliscope/Profile.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <vector>

namespace {

// Slashes, `.`, and `..` are collapsed. `main/../libraries/foo.h` is the same
// file as `libraries/foo.h`. A trailing slash is dropped.
std::string normPath(std::string path) {
  for (char &ch : path) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  const bool absolute = !path.empty() && path.front() == '/';
  std::vector<llvm::StringRef> parts;
  size_t begin = 0;
  auto push = [&](llvm::StringRef part) {
    if (part.empty() || part == ".") {
      return;
    }
    if (part == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (!absolute) {
        parts.push_back(part);
      }
      return;
    }
    parts.push_back(part);
  };
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      push(llvm::StringRef(path.data() + begin, i - begin));
      begin = i + 1;
    }
  }
  std::string out;
  if (absolute) {
    out.push_back('/');
  }
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out.push_back('/');
    }
    out.append(parts[i].data(), parts[i].size());
  }
  return out;
}

// Equal, or one path continues the other after a directory separator.
// `task.cpp` matches `a/task.cpp` and does not match `a/can_task.cpp`.
bool sameFile(llvm::StringRef path, llvm::StringRef spec) {
  if (path == spec) {
    return true;
  }
  const auto boundary = [](llvm::StringRef full, llvm::StringRef tail) {
    return full.size() > tail.size() && full.ends_with(tail) &&
           full[full.size() - tail.size() - 1] == '/';
  };
  return boundary(path, spec) || boundary(spec, path);
}

// `dir` has no trailing slash. The file sits inside that directory. `task`
// matches `task/foo.c` and `a/task/foo.c`, and does not match `can_task/foo.c`.
// The line, column, and message are not part of the match.
bool inTree(llvm::StringRef path, llvm::StringRef dir) {
  if (dir.empty() || path.size() <= dir.size()) {
    return false;
  }
  auto inside = [&](size_t at) {
    if (at + dir.size() >= path.size() || path[at + dir.size()] != '/') {
      return false;
    }
    if (at > 0 && path[at - 1] != '/') {
      return false;
    }
    return path.substr(at, dir.size()) == dir;
  };
  if (inside(0)) {
    return true;
  }
  for (size_t slash = path.find('/'); slash != llvm::StringRef::npos;
       slash = path.find('/', slash + 1)) {
    if (inside(slash + 1)) {
      return true;
    }
  }
  return false;
}

bool ruleId(llvm::StringRef id) {
  if (!id.starts_with("ss.") || id.size() < 6) {
    return false;
  }
  bool secondDot = false;
  for (size_t i = 3; i < id.size(); ++i) {
    const char ch = id[i];
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
    if (!ok) {
      return false;
    }
    if (ch == '.') {
      secondDot = true;
    }
  }
  return secondDot;
}

bool parseNumber(const std::string &text, unsigned &out, bool allowZero) {
  if (text.empty()) {
    return false;
  }
  if (text == "0") {
    if (!allowZero) {
      return false;
    }
    out = 0;
    return true;
  }
  if (text[0] < '1' || text[0] > '9') {
    return false;
  }
  errno = 0;
  char *end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 10);
  if (end != text.c_str() + text.size() || errno == ERANGE ||
      value > std::numeric_limits<unsigned>::max()) {
    return false;
  }
  out = static_cast<unsigned>(value);
  return true;
}

bool summaryLine(llvm::StringRef line) {
  static const llvm::StringRef kKeys[] = {
      "findings: ", "no-checker: ", "suppressed: ", "baseline-missing: "};
  for (const llvm::StringRef key : kKeys) {
    if (!line.starts_with(key)) {
      continue;
    }
    const llvm::StringRef rest = line.substr(key.size());
    unsigned ignored = 0;
    return parseNumber(rest.str(), ignored, true);
  }
  return false;
}

std::string siteKey(unsigned line,
                    unsigned col,
                    llvm::StringRef severity,
                    llvm::StringRef msg,
                    llvm::StringRef id) {
  std::string key;
  key.reserve(severity.size() + msg.size() + id.size() + 24);
  key.append(severity.str());
  key.push_back('\n');
  key.append(msg.str());
  key.push_back('\n');
  key.append(id.str());
  key.push_back('\n');
  key.append(std::to_string(line));
  key.push_back('\n');
  key.append(std::to_string(col));
  return key;
}

struct ParsedDiag {
  std::string file;
  unsigned line = 0;
  unsigned col = 0;
  std::string severity;
  std::string msg;
  std::string id;
};

bool parseDiag(const std::string &line, ParsedDiag &out) {
  if (line.size() < 8 || line.back() != ']') {
    return false;
  }
  const size_t bracket = line.rfind('[');
  if (bracket == std::string::npos || bracket < 2 || line[bracket - 1] != ' ') {
    return false;
  }
  const llvm::StringRef id(line.data() + bracket + 1, line.size() - bracket - 2);
  if (!ruleId(id)) {
    return false;
  }
  const std::string head = line.substr(0, bracket - 1);
  static const char *kSev[] = {"error", "warning", "advisory", "style"};
  size_t at = std::string::npos;
  std::string severity;
  for (const char *name : kSev) {
    const std::string token = std::string(": ") + name + ": ";
    const size_t found = head.rfind(token);
    if (found != std::string::npos && (at == std::string::npos || found > at)) {
      at = found;
      severity = name;
    }
  }
  if (at == std::string::npos || at == 0) {
    return false;
  }
  const std::string where = head.substr(0, at);
  const std::string msg = head.substr(at + severity.size() + 4);
  if (msg.empty()) {
    return false;
  }
  const size_t colColon = where.rfind(':');
  if (colColon == std::string::npos || colColon == 0) {
    return false;
  }
  const size_t lineColon = where.rfind(':', colColon - 1);
  if (lineColon == std::string::npos || lineColon == 0) {
    return false;
  }
  unsigned diagLine = 0;
  unsigned diagCol = 0;
  if (!parseNumber(where.substr(lineColon + 1, colColon - lineColon - 1), diagLine, false) ||
      !parseNumber(where.substr(colColon + 1), diagCol, false)) {
    return false;
  }
  out.file = normPath(where.substr(0, lineColon));
  out.line = diagLine;
  out.col = diagCol;
  out.severity = std::move(severity);
  out.msg = msg;
  out.id = id.str();
  return !out.file.empty();
}

} // namespace

Reporter::Reporter(const Profile &profile) : profile(&profile) {}

bool Reporter::SeenDiag::operator<(const SeenDiag &other) const {
  if (file != other.file) {
    return file < other.file;
  }
  if (line != other.line) {
    return line < other.line;
  }
  if (col != other.col) {
    return col < other.col;
  }
  if (id != other.id) {
    return id < other.id;
  }
  return msg < other.msg;
}

std::optional<DiagSite> Reporter::locate(const clang::SourceManager &sm,
                                         clang::SourceLocation loc) const {
  if (loc.isInvalid()) {
    return std::nullopt;
  }
  // Zephyr's LOG_* macros pass their implementation through as arguments, so
  // the expansion location of every token is the call. The spelling location
  // is the file that wrote the token: the call for an argument, the header
  // for the macro itself. Token paste lives in <scratch space>; step out to
  // the macro that pasted it. Repeated expansions of one token collapse below.
  while (loc.isMacroID() && sm.isWrittenInScratchSpace(sm.getSpellingLoc(loc))) {
    const clang::SourceLocation parent = sm.getImmediateMacroCallerLoc(loc);
    if (parent.isInvalid() || parent == loc) {
      break;
    }
    loc = parent;
  }
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid()) {
    return std::nullopt;
  }
  const clang::PresumedLoc pl = sm.getPresumedLoc(loc);
  if (pl.isInvalid() || !pl.getFilename()) {
    return std::nullopt;
  }
  DiagSite site;
  site.file = pl.getFilename();
  site.line = pl.getLine();
  site.col = pl.getColumn();
  const bool dropped =
      sm.isInSystemHeader(loc) || sm.isWrittenInScratchSpace(loc) || sm.isInPredefinedFile(loc);
  site.reportable = !dropped && !site.file.empty();
  return site;
}

bool Reporter::addSuppress(const std::string &text, std::string &err) {
  const llvm::StringRef spec(text);
  SuppressSpec parsed;
  std::string rawPath;
  const auto fail = [&] {
    err = "--suppress wants a file, ss.rule:file, or a directory ending in / (got " + text + ")";
    return false;
  };
  if (spec.starts_with("ss.")) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || !ruleId(llvm::StringRef(text).take_front(colon)) ||
        colon + 1 >= text.size()) {
      return fail();
    }
    parsed.rule = text.substr(0, colon);
    rawPath = text.substr(colon + 1);
  } else {
    rawPath = text;
  }
  parsed.tree = !rawPath.empty() && (rawPath.back() == '/' || rawPath.back() == '\\');
  parsed.path = normPath(std::move(rawPath));
  // `/` would quiet every file. `.` and `..` are not a directory the user named.
  if (parsed.path.empty() || parsed.path == ".." || parsed.path == "/") {
    return fail();
  }
  suppress.push_back(std::move(parsed));
  filter_active = true;
  return true;
}

bool Reporter::loadBaseline(const std::string &path, std::string &err) {
  std::ifstream in(path);
  if (!in) {
    err = "cannot read baseline " + path;
    return false;
  }
  baseline_active = true;
  filter_active = true;
  std::set<std::string> seenLine;
  std::string line;
  unsigned number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty() || summaryLine(line)) {
      continue;
    }
    ParsedDiag parsed;
    if (!parseDiag(line, parsed)) {
      err = "baseline " + path + ":" + std::to_string(number) + ": unrecognized line";
      return false;
    }
    const std::string full =
        siteKey(parsed.line, parsed.col, parsed.severity, parsed.msg, parsed.id) + "\n" +
        parsed.file;
    if (!seenLine.insert(full).second) {
      continue;
    }
    const unsigned index = static_cast<unsigned>(baseline.size());
    baseline_at[siteKey(parsed.line, parsed.col, parsed.severity, parsed.msg, parsed.id)].push_back(
        index);
    BaselineEntry entry;
    entry.file = std::move(parsed.file);
    entry.line = parsed.line;
    entry.col = parsed.col;
    entry.severity = std::move(parsed.severity);
    entry.msg = std::move(parsed.msg);
    entry.id = std::move(parsed.id);
    baseline.push_back(std::move(entry));
  }
  return true;
}

bool Reporter::claimBaseline(const SeenDiag &key, const char *severity) {
  if (!baseline_active || !severity) {
    return false;
  }
  const std::string file = normPath(key.file);
  const auto found = baseline_at.find(siteKey(key.line, key.col, severity, key.msg, key.id));
  if (found == baseline_at.end()) {
    return false;
  }
  unsigned best = 0;
  size_t bestLen = 0;
  bool matched = false;
  for (const unsigned index : found->second) {
    const BaselineEntry &entry = baseline[index];
    if (!sameFile(file, entry.file)) {
      continue;
    }
    if (!matched || entry.file.size() >= bestLen) {
      best = index;
      bestLen = entry.file.size();
      matched = true;
    }
  }
  if (!matched) {
    return false;
  }
  baseline[best].used = true;
  return true;
}

bool Reporter::drop(const SeenDiag &key, const char *severity) {
  bool file = false;
  const std::string path = normPath(key.file);
  for (const SuppressSpec &spec : suppress) {
    if (!spec.rule.empty() && spec.rule != key.id) {
      continue;
    }
    const bool hit = spec.tree ? inTree(path, spec.path) : sameFile(path, spec.path);
    if (hit) {
      file = true;
      break;
    }
  }
  const bool saved = claimBaseline(key, severity);
  return file || saved;
}

void Reporter::emitSite(const DiagSite &site, const char *id, const char *msg) {
  const char *severity = profile->severityOf(id);
  if (!severity || !site.reportable || site.file.empty()) {
    return;
  }
  const SeenDiag key{site.file, site.line, site.col, id, msg};
  if (!seen.insert(key).second) {
    return;
  }
  if (filter_active && drop(key, severity)) {
    ++suppressed;
    return;
  }
  ++findings;
  llvm::outs() << site.file << ":" << site.line << ":" << site.col << ": " << severity << ": "
               << msg << " [" << id << "]\n";
}

void Reporter::emit(const clang::SourceManager &sm,
                    clang::SourceLocation loc,
                    const char *id,
                    const char *msg) {
  const std::optional<DiagSite> site = locate(sm, loc);
  if (!site) {
    return;
  }
  emitSite(*site, id, msg);
}

void Reporter::printSummary() const {
  llvm::outs() << "findings: " << findings << "\n";
  if (filter_active) {
    llvm::outs() << "suppressed: " << suppressed << "\n";
  }
  if (baseline_active) {
    unsigned missing = 0;
    for (const BaselineEntry &entry : baseline) {
      if (!entry.used) {
        ++missing;
      }
    }
    llvm::outs() << "baseline-missing: " << missing << "\n";
  }
}

bool Reporter::allows(const char *id, const char *name) const {
  return profile->allows(id, name);
}

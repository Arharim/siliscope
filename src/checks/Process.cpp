#include "siliscope/Process.h"

#include "siliscope/Report.h"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <mutex>
#include <optional>

using clang::ASTContext;
using clang::FileID;
using clang::SourceLocation;
using clang::SourceManager;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kWarnings = "ss.proc.all-warnings";
constexpr const char *kWarningsMsg =
    "compile this file with -Wall -Wextra -Wpedantic -Werror and no -Wno-";
constexpr const char *kPragmaMsg = "do not hide a diagnostic with a pragma";

std::mutex gCommandMu;
llvm::StringMap<std::vector<std::string>> gCommands;

std::string canon(llvm::StringRef path) {
  llvm::SmallString<256> abs(path);
  llvm::sys::fs::make_absolute(abs);
  llvm::sys::path::remove_dots(abs, /*remove_dot_dot=*/true);
  return std::string(abs);
}

bool strictWarnings(const std::vector<std::string> &args) {
  bool wall = false;
  bool extra = false;
  bool pedantic = false;
  bool werror = false;
  bool disabled = false;
  for (const std::string &arg : args) {
    if (arg == "-Wall") {
      wall = true;
    } else if (arg == "-Wextra") {
      extra = true;
    } else if (arg == "-Wpedantic" || arg == "-pedantic") {
      pedantic = true;
    } else if (arg == "-Werror") {
      werror = true;
    } else if (arg == "-w" || llvm::StringRef(arg).starts_with("-Wno-")) {
      disabled = true;
    }
  }
  return wall && extra && pedantic && werror && !disabled;
}

std::optional<std::vector<std::string>> commandFor(llvm::StringRef path) {
  const std::string key = canon(path);
  std::lock_guard<std::mutex> lock(gCommandMu);
  const auto it = gCommands.find(key);
  if (it == gCommands.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool hidesDiagnostic(llvm::StringRef line) {
  llvm::StringRef text = line.ltrim(" \t");
  if (!text.consume_front("#")) {
    return false;
  }
  text = text.ltrim(" \t");
  if (!text.consume_front("pragma")) {
    return false;
  }
  text = text.ltrim(" \t");
  if (text.starts_with("GCC") || text.starts_with("clang")) {
    return text.contains("diagnostic") && text.contains("ignored");
  }
  return text.starts_with("warning") && text.contains("disable");
}

void scanPragmas(const SourceManager &sm, FileID file, Reporter &reporter) {
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(file, &invalid);
  if (invalid || buf.empty()) {
    return;
  }
  const SourceLocation origin = sm.getLocForStartOfFile(file);
  bool lineComment = false;
  bool block = false;
  char quote = 0;
  unsigned lineStart = 0;
  for (size_t i = 0; i < buf.size(); ++i) {
    const char c = buf[i];
    if (c == '\n') {
      if (!lineComment && !block && !quote) {
        const llvm::StringRef line = buf.slice(lineStart, i);
        if (hidesDiagnostic(line)) {
          reporter.emit(
              sm, origin.getLocWithOffset(static_cast<int>(lineStart)), kWarnings, kPragmaMsg);
        }
      }
      lineComment = false;
      quote = 0;
      lineStart = static_cast<unsigned>(i + 1);
      continue;
    }
    if (quote) {
      if (c == '\\' && i + 1 < buf.size()) {
        ++i;
      } else if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (lineComment || block) {
      if (block && c == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
        block = false;
        ++i;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      lineComment = true;
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      block = true;
      ++i;
    }
  }
  if (!lineComment && !block && !quote && lineStart < buf.size() &&
      hidesDiagnostic(buf.substr(lineStart))) {
    reporter.emit(sm, origin.getLocWithOffset(static_cast<int>(lineStart)), kWarnings, kPragmaMsg);
  }
}

void checkWarnings(const SourceManager &sm, Reporter &reporter) {
  const FileID main = sm.getMainFileID();
  const SourceLocation start = sm.getLocForStartOfFile(main);
  if (const auto cmd = commandFor(sm.getFilename(start))) {
    if (!strictWarnings(*cmd)) {
      reporter.emit(sm, start, kWarnings, kWarningsMsg);
    }
  }
  // The running siliscope is the analyzer the catalog asks for. A second
  // analyzer is only preferred, so ss.proc.static-analysis adds no finding.
  scanPragmas(sm, main, reporter);
}

} // namespace

void rememberCompileCommand(llvm::StringRef file, const std::vector<std::string> &args) {
  if (file.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(gCommandMu);
  gCommands[canon(file)] = args;
}

void ProcessCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void ProcessCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager ||
      !result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu")) {
    return;
  }
  // Both rule ids share this class, so the pass runs twice. The reporter
  // keeps one copy of the same finding.
  checkWarnings(*result.SourceManager, reporter);
}

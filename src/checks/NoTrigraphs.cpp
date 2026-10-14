#include "siliscope/NoTrigraphs.h"

#include "siliscope/Report.h"

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using clang::FileID;
using clang::SourceLocation;
using clang::SourceManager;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.expr.no-trigraphs";
constexpr const char *kTrigraph = "do not use a trigraph";
constexpr const char *kDigraph = "do not use a digraph";

bool projectFile(const SourceManager &sm, FileID file) {
  if (file.isInvalid()) {
    return false;
  }
  const SourceLocation start = sm.getLocForStartOfFile(file);
  return start.isValid() && !sm.isInSystemHeader(start) && !sm.isWrittenInBuiltinFile(start) &&
         !sm.isWrittenInScratchSpace(start) && !sm.isInPredefinedFile(start) &&
         !sm.isWrittenInCommandLineFile(start);
}

bool trigraphChar(char c) {
  switch (c) {
  case '=':
  case '/':
  case '\'':
  case '(':
  case ')':
  case '!':
  case '<':
  case '>':
  case '-':
    return true;
  default:
    return false;
  }
}

void scan(const SourceManager &sm, FileID file, llvm::StringRef buf, Reporter &reporter) {
  bool inLine = false;
  bool inBlock = false;
  char quote = 0;
  const SourceLocation origin = sm.getLocForStartOfFile(file);
  const auto emit = [&](unsigned offset, const char *msg) {
    reporter.emit(sm, origin.getLocWithOffset(static_cast<int>(offset)), kId, msg);
  };
  for (size_t i = 0; i < buf.size(); ++i) {
    const char c = buf[i];
    if (quote) {
      if (c == '\\' && i + 1 < buf.size()) {
        ++i;
        continue;
      }
      if (c == '?' && i + 2 < buf.size() && buf[i + 1] == '?' && trigraphChar(buf[i + 2])) {
        emit(static_cast<unsigned>(i), kTrigraph);
      }
      if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (inLine) {
      if (c == '?' && i + 2 < buf.size() && buf[i + 1] == '?' && trigraphChar(buf[i + 2])) {
        emit(static_cast<unsigned>(i), kTrigraph);
      }
      if (c == '\n') {
        inLine = false;
      }
      continue;
    }
    if (inBlock) {
      if (c == '?' && i + 2 < buf.size() && buf[i + 1] == '?' && trigraphChar(buf[i + 2])) {
        emit(static_cast<unsigned>(i), kTrigraph);
      }
      if (c == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
        inBlock = false;
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      inLine = true;
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      inBlock = true;
      ++i;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '?' && i + 2 < buf.size() && buf[i + 1] == '?' && trigraphChar(buf[i + 2])) {
      emit(static_cast<unsigned>(i), kTrigraph);
      continue;
    }
    if (i + 1 >= buf.size()) {
      continue;
    }
    const char next = buf[i + 1];
    const bool digraph = (c == '<' && (next == ':' || next == '%')) ||
                         (c == '%' && (next == '>' || next == ':')) || (c == ':' && next == '>');
    if (digraph) {
      emit(static_cast<unsigned>(i), kDigraph);
    }
  }
}

} // namespace

void NoTrigraphsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoTrigraphsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu") || !result.SourceManager) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
    const FileID file = sm.translateFile(it->getFirst());
    if (!projectFile(sm, file)) {
      continue;
    }
    bool invalid = false;
    const llvm::StringRef buf = sm.getBufferData(file, &invalid);
    if (invalid || buf.empty()) {
      continue;
    }
    scan(sm, file, buf, reporter);
  }
}

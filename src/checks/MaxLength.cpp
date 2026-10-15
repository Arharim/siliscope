#include "siliscope/MaxLength.h"

#include "siliscope/Report.h"

#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <cctype>
#include <string>

using clang::CompoundStmt;
using clang::FunctionDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.fn.max-length";
constexpr const char *kMsg = "keep this function to 80 lines";
constexpr unsigned kLimit = 80;

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

// Comments and blank lines are not body length. A comment-padded function stays quiet.
unsigned codeLines(SourceRange range, const SourceManager &sm) {
  const SourceLocation begin = sm.getSpellingLoc(range.getBegin());
  const SourceLocation end = sm.getSpellingLoc(range.getEnd());
  if (begin.isInvalid() || end.isInvalid() || sm.getFileID(begin) != sm.getFileID(end)) {
    return 0;
  }
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(sm.getFileID(begin), &invalid);
  if (invalid || buf.empty()) {
    return 0;
  }
  const unsigned from = sm.getFileOffset(begin);
  unsigned to = sm.getFileOffset(end);
  if (from >= buf.size()) {
    return 0;
  }
  if (to >= buf.size()) {
    to = static_cast<unsigned>(buf.size() - 1);
  }
  unsigned lines = 0;
  bool code = false;
  bool lineComment = false;
  bool block = false;
  bool quoted = false;
  char quote = 0;
  for (unsigned i = from; i <= to && i < buf.size(); ++i) {
    const char c = buf[i];
    if (c == '\n') {
      if (code) {
        ++lines;
      }
      code = false;
      lineComment = false;
      quoted = false;
      continue;
    }
    if (lineComment) {
      continue;
    }
    if (block) {
      if (c == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
        block = false;
        ++i;
      }
      continue;
    }
    if (quoted) {
      code = true;
      if (c == '\\' && i + 1 < buf.size()) {
        ++i;
        continue;
      }
      if (c == quote) {
        quoted = false;
      }
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
      continue;
    }
    if (c == '"' || c == '\'') {
      quoted = true;
      quote = c;
      code = true;
      continue;
    }
    if (!std::isspace(static_cast<unsigned char>(c))) {
      code = true;
    }
  }
  if (code) {
    ++lines;
  }
  return lines;
}

} // namespace

void MaxLengthCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void MaxLengthCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.SourceManager || !fn->getBody() || allowed(fn, reporter)) {
    return;
  }
  const auto *body = llvm::dyn_cast<CompoundStmt>(fn->getBody());
  if (!body) {
    return;
  }
  if (codeLines(body->getSourceRange(), *result.SourceManager) <= kLimit) {
    return;
  }
  reporter.emit(*result.SourceManager, fn->getLocation(), kId, kMsg);
}

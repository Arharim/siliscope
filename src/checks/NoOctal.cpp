#include "siliscope/NoOctal.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

using clang::IntegerLiteral;
using clang::Lexer;
using clang::ast_matchers::integerLiteral;

static bool isOctalIntegerSpelling(llvm::StringRef s) {
  if (s.size() < 2 || s.front() != '0') {
    return false;
  }
  const char n = s[1];
  if (n == 'x' || n == 'X' || n == 'b' || n == 'B') {
    return false;
  }
  if (n == 'o' || n == 'O') {
    return true;
  }
  for (size_t i = 1; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '\'') {
      continue;
    }
    return c >= '0' && c <= '7';
  }
  return false;
}

void NoOctalCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(integerLiteral().bind("lit"), this);
}

void NoOctalCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *lit = result.Nodes.getNodeAs<IntegerLiteral>("lit");
  if (!lit || !result.SourceManager || !result.Context) {
    return;
  }
  llvm::SmallString<32> buf;
  const llvm::StringRef spelling = Lexer::getSpelling(
      lit->getLocation(), buf, *result.SourceManager, result.Context->getLangOpts());
  if (!isOctalIntegerSpelling(spelling)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                lit->getLocation(),
                "ss.expr.no-octal",
                "do not use octal integer literals");
}

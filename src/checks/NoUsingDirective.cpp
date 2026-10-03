#include "siliscope/NoUsingDirective.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::UsingDirectiveDecl;
using clang::ast_matchers::usingDirectiveDecl;

void NoUsingDirectiveCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(usingDirectiveDecl().bind("dir"), this);
}

void NoUsingDirectiveCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *dir = result.Nodes.getNodeAs<UsingDirectiveDecl>("dir");
  if (!dir) {
    return;
  }
  const clang::SourceLocation loc =
      dir->getUsingLoc().isValid() ? dir->getUsingLoc() : dir->getLocation();
  reporter.emit(
      *result.SourceManager, loc, "ss.cpp.no-using-directive", "do not use a using-directive");
}

#include "siliscope/NoVLA.h"

#include "siliscope/Report.h"

#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::TypeLoc;
using clang::VariableArrayTypeLoc;
using clang::ast_matchers::loc;
using clang::ast_matchers::typeLoc;
using clang::ast_matchers::variableArrayType;

void NoVLACheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(typeLoc(loc(variableArrayType())).bind("vla"), this);
}

void NoVLACheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tl = result.Nodes.getNodeAs<TypeLoc>("vla");
  if (!tl || tl->isNull()) {
    return;
  }
  clang::SourceLocation at = tl->getAs<VariableArrayTypeLoc>().getLBracketLoc();
  if (at.isInvalid()) {
    at = tl->getBeginLoc();
  }
  reporter.emit(
      *result.SourceManager, at, "error", "ss.mem.no-vla", "do not use variable-length arrays");
}

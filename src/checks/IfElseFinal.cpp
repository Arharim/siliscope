#include "siliscope/IfElseFinal.h"

#include "siliscope/Report.h"

#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::IfStmt;
using clang::ast_matchers::hasElse;
using clang::ast_matchers::ifStmt;
using clang::ast_matchers::stmt;
using clang::ast_matchers::unless;

void IfElseFinalCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(ifStmt(hasElse(ifStmt(unless(hasElse(stmt()))).bind("if"))), this);
}

void IfElseFinalCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *s = result.Nodes.getNodeAs<IfStmt>("if");
  if (!s) {
    return;
  }
  reporter.emit(*result.SourceManager,
                s->getIfLoc(),
                "ss.ctrl.if-else-final",
                "terminate this if/else-if chain with else");
}

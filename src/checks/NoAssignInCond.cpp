#include "siliscope/NoAssignInCond.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::ast_matchers::anyOf;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::expr;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::hasCondition;
using clang::ast_matchers::hasDescendant;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::ifStmt;
using clang::ast_matchers::switchStmt;
using clang::ast_matchers::whileStmt;

void NoAssignInCondCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  // Bind separately: a Matcher with bind() must not be reused in the same tree.
  auto cond = expr(anyOf(binaryOperator(hasOperatorName("=")).bind("assign"),
                         hasDescendant(binaryOperator(hasOperatorName("=")).bind("assign"))));
  finder.addMatcher(ifStmt(hasCondition(cond)), this);
  finder.addMatcher(whileStmt(hasCondition(cond)), this);
  finder.addMatcher(doStmt(hasCondition(cond)), this);
  finder.addMatcher(switchStmt(hasCondition(cond)), this);
  finder.addMatcher(forStmt(hasCondition(cond)), this);
}

void NoAssignInCondCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *op = result.Nodes.getNodeAs<BinaryOperator>("assign");
  if (!op) {
    return;
  }
  reporter.emit(*result.SourceManager,
                op->getOperatorLoc(),
                "warning",
                "ss.ctrl.no-assignment-in-condition",
                "do not assign in a controlling expression");
}

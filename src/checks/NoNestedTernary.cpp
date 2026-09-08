#include "siliscope/NoNestedTernary.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::ConditionalOperator;
using clang::ast_matchers::conditionalOperator;
using clang::ast_matchers::hasDescendant;

void NoNestedTernaryCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(conditionalOperator(hasDescendant(conditionalOperator())).bind("tern"), this);
}

void NoNestedTernaryCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *op = result.Nodes.getNodeAs<ConditionalOperator>("tern");
  if (!op) {
    return;
  }
  reporter.emit(*result.SourceManager,
                op->getQuestionLoc(),
                "ss.ctrl.no-nested-ternary",
                "do not nest the ternary operator");
}

#include "siliscope/NoSizeofSideEffect.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::GenericSelectionExpr;
using clang::UETT_AlignOf;
using clang::UETT_PreferredAlignOf;
using clang::UETT_SizeOf;
using clang::UnaryExprOrTypeTraitExpr;
using clang::ast_matchers::anyOf;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::expr;
using clang::ast_matchers::genericSelectionExpr;
using clang::ast_matchers::hasDescendant;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::isAssignmentOperator;
using clang::ast_matchers::ofKind;
using clang::ast_matchers::unaryExprOrTypeTraitExpr;
using clang::ast_matchers::unaryOperator;

void NoSizeofSideEffectCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  const auto effect = expr(anyOf(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--"))),
                                 callExpr(),
                                 binaryOperator(isAssignmentOperator())));
  finder.addMatcher(
      unaryExprOrTypeTraitExpr(
          anyOf(ofKind(UETT_SizeOf), ofKind(UETT_AlignOf), ofKind(UETT_PreferredAlignOf)),
          hasDescendant(effect))
          .bind("uett"),
      this);
  finder.addMatcher(genericSelectionExpr(hasDescendant(effect)).bind("generic"), this);
}

void NoSizeofSideEffectCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  clang::SourceLocation at;
  if (const auto *u = result.Nodes.getNodeAs<UnaryExprOrTypeTraitExpr>("uett")) {
    at = u->getOperatorLoc();
  } else if (const auto *g = result.Nodes.getNodeAs<GenericSelectionExpr>("generic")) {
    at = g->getGenericLoc();
  } else {
    return;
  }
  reporter.emit(*result.SourceManager,
                at,
                "ss.expr.no-sizeof-side-effect",
                "operand of sizeof/_Alignof/_Generic must be side-effect free");
}

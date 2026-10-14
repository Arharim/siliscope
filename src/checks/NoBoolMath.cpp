#include "siliscope/NoBoolMath.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::BinaryOperator;
using clang::Expr;
using clang::FunctionDecl;
using clang::UnaryOperator;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::unaryOperator;

namespace {

constexpr const char *kId = "ss.type.no-bool-math";
constexpr const char *kMsg = "do not do arithmetic on a boolean";

bool boolean(const Expr *expr) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  return expr && !expr->isTypeDependent() && expr->getType()->isBooleanType();
}

bool arithmetic(clang::BinaryOperator::Opcode op) {
  switch (op) {
  case clang::BO_Add:
  case clang::BO_Sub:
  case clang::BO_Mul:
  case clang::BO_Div:
  case clang::BO_Rem:
  case clang::BO_AddAssign:
  case clang::BO_SubAssign:
  case clang::BO_MulAssign:
  case clang::BO_DivAssign:
  case clang::BO_RemAssign:
    return true;
  default:
    return false;
  }
}

bool allowed(const clang::Stmt *stmt, clang::ASTContext &ctx, Reporter &reporter) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*stmt);
  for (int depth = 0; depth < 32; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      if (fn->getDeclName().isEmpty()) {
        return false;
      }
      const std::string name = fn->getDeclName().getAsString();
      return !name.empty() && reporter.allows(kId, name.c_str());
    }
  }
  return false;
}

} // namespace

void NoBoolMathCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(unaryOperator().bind("unary"), this);
  finder.addMatcher(binaryOperator().bind("binary"), this);
}

void NoBoolMathCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  if (const auto *unary = result.Nodes.getNodeAs<UnaryOperator>("unary")) {
    switch (unary->getOpcode()) {
    case clang::UO_PostInc:
    case clang::UO_PostDec:
    case clang::UO_PreInc:
    case clang::UO_PreDec:
      break;
    default:
      return;
    }
    if (!boolean(unary->getSubExpr()) || allowed(unary, *result.Context, reporter)) {
      return;
    }
    reporter.emit(*result.SourceManager, unary->getOperatorLoc(), kId, kMsg);
    return;
  }
  const auto *binary = result.Nodes.getNodeAs<BinaryOperator>("binary");
  if (!binary || !arithmetic(binary->getOpcode())) {
    return;
  }
  if (!boolean(binary->getLHS()) && !boolean(binary->getRHS())) {
    return;
  }
  if (allowed(binary, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, binary->getOperatorLoc(), kId, kMsg);
}

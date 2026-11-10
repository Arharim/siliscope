#include "siliscope/NoMixedCategories.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::BinaryOperator;
using clang::Expr;
using clang::FunctionDecl;
using clang::QualType;
using clang::SourceLocation;
using clang::SourceManager;
using clang::UnaryOperator;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::unaryOperator;

namespace {

constexpr const char *kId = "ss.conv.no-mixed-categories";
constexpr const char *kMsg = "do not use a bool, character, or enum as an integer here";

bool project(const SourceManager &sm, SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  return loc.isValid() && !sm.isInSystemHeader(loc) && !sm.isWrittenInBuiltinFile(loc) &&
         !sm.isWrittenInScratchSpace(loc) && !sm.isInPredefinedFile(loc) &&
         !sm.isWrittenInCommandLineFile(loc);
}

bool category(const Expr *expr) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  if (!expr || expr->isTypeDependent()) {
    return false;
  }
  const QualType type = expr->getType();
  return type->isBooleanType() || type->isAnyCharacterType() || type->isEnumeralType();
}

bool integerOp(clang::BinaryOperator::Opcode op) {
  switch (op) {
  case clang::BO_Add:
  case clang::BO_Sub:
  case clang::BO_Mul:
  case clang::BO_Div:
  case clang::BO_Rem:
  case clang::BO_Shl:
  case clang::BO_Shr:
  case clang::BO_And:
  case clang::BO_Xor:
  case clang::BO_Or:
  case clang::BO_AddAssign:
  case clang::BO_SubAssign:
  case clang::BO_MulAssign:
  case clang::BO_DivAssign:
  case clang::BO_RemAssign:
  case clang::BO_ShlAssign:
  case clang::BO_ShrAssign:
  case clang::BO_AndAssign:
  case clang::BO_XorAssign:
  case clang::BO_OrAssign:
    return true;
  default:
    return false;
  }
}

bool integerUnary(clang::UnaryOperator::Opcode op) {
  switch (op) {
  case clang::UO_Plus:
  case clang::UO_Minus:
  case clang::UO_Not:
  case clang::UO_PreInc:
  case clang::UO_PostInc:
  case clang::UO_PreDec:
  case clang::UO_PostDec:
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

void NoMixedCategoriesCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("binary"), this);
  finder.addMatcher(unaryOperator().bind("unary"), this);
}

void NoMixedCategoriesCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager || result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  if (const auto *bin = result.Nodes.getNodeAs<BinaryOperator>("binary")) {
    if (!integerOp(bin->getOpcode()) || (!category(bin->getLHS()) && !category(bin->getRHS())) ||
        !project(sm, bin->getOperatorLoc()) || allowed(bin, *result.Context, reporter)) {
      return;
    }
    reporter.emit(sm, bin->getOperatorLoc(), kId, kMsg);
    return;
  }
  if (const auto *unary = result.Nodes.getNodeAs<UnaryOperator>("unary")) {
    if (!integerUnary(unary->getOpcode()) || !category(unary->getSubExpr()) ||
        !project(sm, unary->getOperatorLoc()) || allowed(unary, *result.Context, reporter)) {
      return;
    }
    reporter.emit(sm, unary->getOperatorLoc(), kId, kMsg);
  }
}

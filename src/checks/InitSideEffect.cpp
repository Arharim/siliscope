#include "siliscope/InitSideEffect.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::BinaryOperator;
using clang::CallExpr;
using clang::CXXDeleteExpr;
using clang::CXXNewExpr;
using clang::Expr;
using clang::InitListExpr;
using clang::NamedDecl;
using clang::UnaryExprOrTypeTraitExpr;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::cxxConstructorDecl;
using clang::ast_matchers::hasInitializer;
using clang::ast_matchers::initListExpr;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.expr.init-side-effect-free";
constexpr const char *kMsg = "an initializer has no side effects";

bool allowed(const NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool harmful(const Expr *expr, clang::SourceLocation &at) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  if (!expr || llvm::isa<UnaryExprOrTypeTraitExpr>(expr)) {
    return false;
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(expr)) {
    switch (unary->getOpcode()) {
    case clang::UO_PostInc:
    case clang::UO_PostDec:
    case clang::UO_PreInc:
    case clang::UO_PreDec:
      at = unary->getOperatorLoc();
      return true;
    default:
      return harmful(unary->getSubExpr(), at);
    }
  }
  if (const auto *binary = llvm::dyn_cast<BinaryOperator>(expr)) {
    if (binary->isAssignmentOp()) {
      at = binary->getOperatorLoc();
      return true;
    }
    return harmful(binary->getLHS(), at) || harmful(binary->getRHS(), at);
  }
  if (llvm::isa<CallExpr>(expr) || llvm::isa<CXXNewExpr>(expr) || llvm::isa<CXXDeleteExpr>(expr)) {
    at = expr->getBeginLoc();
    return true;
  }
  if (const auto *list = llvm::dyn_cast<InitListExpr>(expr)) {
    const InitListExpr *written = list;
    if (list->isSemanticForm() && list->getSyntacticForm()) {
      written = list->getSyntacticForm();
    }
    for (const Expr *child : written->inits()) {
      if (harmful(child, at)) {
        return true;
      }
    }
    return false;
  }
  for (const clang::Stmt *child : expr->children()) {
    if (const auto *inner = llvm::dyn_cast_or_null<Expr>(child)) {
      if (harmful(inner, at)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

void InitSideEffectCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl(hasInitializer(initListExpr())).bind("var"), this);
  finder.addMatcher(cxxConstructorDecl(isDefinition()).bind("ctor"), this);
}

void InitSideEffectCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context) {
    return;
  }
  if (const auto *var = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (var->isImplicit() || allowed(var, reporter) || !var->getInit()) {
      return;
    }
    clang::SourceLocation at;
    if (!harmful(var->getInit(), at)) {
      return;
    }
    reporter.emit(*result.SourceManager, at, kId, kMsg);
    return;
  }
  const auto *ctor = result.Nodes.getNodeAs<clang::CXXConstructorDecl>("ctor");
  if (!ctor || allowed(ctor, reporter)) {
    return;
  }
  for (const clang::CXXCtorInitializer *init : ctor->inits()) {
    if (!init || !init->isWritten() || !init->getInit()) {
      continue;
    }
    clang::SourceLocation at;
    if (!harmful(init->getInit(), at)) {
      continue;
    }
    reporter.emit(*result.SourceManager, at, kId, kMsg);
  }
}

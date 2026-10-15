#include "siliscope/DefaultIsr.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::Expr;
using clang::ImplicitValueInitExpr;
using clang::InitListExpr;
using clang::SectionAttr;
using clang::VarDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.emb.default-isr";
constexpr const char *kMsg = "give this vector a default handler";

bool allowed(const VarDecl *var, Reporter &reporter) {
  if (!var || !var->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = var->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool vectorTable(const VarDecl *var) {
  bool named = false;
  if (var->getDeclName().isIdentifier()) {
    named = var->getName().contains_insensitive("vector");
  }
  bool section = false;
  if (const auto *attr = var->getAttr<SectionAttr>()) {
    section = attr->getName().contains_insensitive("vector");
  }
  if (!named && !section) {
    return false;
  }
  const clang::ArrayType *array = var->getType()->getAsArrayTypeUnsafe();
  return array && array->getElementType()->isFunctionPointerType();
}

bool nullSlot(const Expr *expr, clang::ASTContext &ctx) {
  if (!expr) {
    return true;
  }
  expr = expr->IgnoreParenImpCasts();
  if (llvm::isa<ImplicitValueInitExpr>(expr)) {
    return true;
  }
  return expr->isNullPointerConstant(ctx, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull;
}

} // namespace

void DefaultIsrCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl(isDefinition(), unless(isImplicit())).bind("var"), this);
}

void DefaultIsrCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *var = result.Nodes.getNodeAs<VarDecl>("var");
  if (!var || !result.SourceManager || !result.Context || !vectorTable(var) ||
      allowed(var, reporter)) {
    return;
  }
  const Expr *init = var->getInit() ? var->getInit()->IgnoreParenImpCasts() : nullptr;
  const auto *list = init ? llvm::dyn_cast<InitListExpr>(init) : nullptr;
  if (list && !list->isSemanticForm() && list->getSemanticForm()) {
    list = list->getSemanticForm();
  }
  if (!list) {
    reporter.emit(*result.SourceManager, var->getLocation(), kId, kMsg);
    return;
  }
  for (unsigned i = 0; i < list->getNumInits(); ++i) {
    const Expr *slot = list->getInit(i);
    if (!nullSlot(slot, *result.Context)) {
      continue;
    }
    const clang::SourceLocation at =
        slot && slot->getExprLoc().isValid() ? slot->getExprLoc() : var->getLocation();
    reporter.emit(*result.SourceManager, at, kId, kMsg);
  }
  if (list->hasArrayFiller() && nullSlot(list->getArrayFiller(), *result.Context)) {
    reporter.emit(*result.SourceManager, var->getLocation(), kId, kMsg);
  }
}

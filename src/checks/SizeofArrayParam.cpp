#include "siliscope/SizeofArrayParam.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::DeclRefExpr;
using clang::ParmVarDecl;
using clang::UETT_SizeOf;
using clang::UnaryExprOrTypeTraitExpr;
using clang::ast_matchers::ofKind;
using clang::ast_matchers::unaryExprOrTypeTraitExpr;

void SizeofArrayParamCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(unaryExprOrTypeTraitExpr(ofKind(UETT_SizeOf)).bind("sz"), this);
}

void SizeofArrayParamCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *sz = result.Nodes.getNodeAs<UnaryExprOrTypeTraitExpr>("sz");
  if (!sz || !result.SourceManager || sz->isArgumentType()) {
    return;
  }
  const clang::Expr *arg = sz->getArgumentExpr();
  if (!arg || arg->isTypeDependent()) {
    return;
  }
  arg = arg->IgnoreParenImpCasts();
  const auto *ref = llvm::dyn_cast_or_null<DeclRefExpr>(arg);
  if (!ref) {
    return;
  }
  const auto *parm = llvm::dyn_cast<ParmVarDecl>(ref->getDecl());
  if (!parm || parm->getType().isNull() || !parm->getType()->isPointerType()) {
    return;
  }
  reporter.emit(*result.SourceManager,
                arg->getExprLoc(),
                "ss.mem.sizeof-array-param",
                "sizeof of a decayed array parameter is the pointer size");
}

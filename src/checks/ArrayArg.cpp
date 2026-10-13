#include "siliscope/ArrayArg.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <algorithm>
#include <optional>

using clang::CallExpr;
using clang::ParmVarDecl;
using clang::QualType;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::unless;

namespace {

std::optional<uint64_t> outermostBound(clang::ASTContext &ctx, QualType t) {
  if (t.isNull() || t->isDependentType()) {
    return std::nullopt;
  }
  const clang::ConstantArrayType *cat = ctx.getAsConstantArrayType(t);
  if (!cat) {
    return std::nullopt;
  }
  const llvm::APInt &sz = cat->getSize();
  if (sz.getActiveBits() > 64) {
    return std::nullopt;
  }
  return sz.getZExtValue();
}

} // namespace

void ArrayArgCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(unless(clang::ast_matchers::cxxOperatorCallExpr())).bind("call"),
                    this);
}

void ArrayArgCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  if (!call || !result.Context || !result.SourceManager || call->isTypeDependent()) {
    return;
  }
  const clang::FunctionDecl *callee = call->getDirectCallee();
  if (!callee) {
    return;
  }
  clang::ASTContext &ctx = *result.Context;
  const unsigned n = std::min(call->getNumArgs(), callee->getNumParams());
  for (unsigned i = 0; i < n; ++i) {
    const ParmVarDecl *parm = callee->getParamDecl(i);
    if (!parm) {
      continue;
    }
    const QualType original = parm->getOriginalType();
    if (original.isNull() || original->isDependentType()) {
      continue;
    }
    // int a[] and int * have no constant N. Only the outermost bound counts.
    const std::optional<uint64_t> need = outermostBound(ctx, original);
    if (!need) {
      continue;
    }
    const clang::Expr *arg = call->getArg(i);
    if (!arg) {
      continue;
    }
    arg = arg->IgnoreParenImpCasts();
    if (!arg || arg->isTypeDependent()) {
      continue;
    }
    const std::optional<uint64_t> have = outermostBound(ctx, arg->getType());
    if (have && *have >= *need) {
      continue;
    }
    reporter.emit(*result.SourceManager,
                  arg->getExprLoc(),
                  "ss.ptr.array-arg",
                  "pass an array of at least the declared length");
  }
}

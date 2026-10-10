#include "siliscope/NoIntToEnum.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CastExpr;
using clang::Expr;
using clang::QualType;
using clang::ast_matchers::castExpr;

namespace {

static bool sameEnum(QualType from, QualType to) {
  if (from.isNull() || to.isNull() || !from->isEnumeralType() || !to->isEnumeralType()) {
    return false;
  }
  return from.getCanonicalType().getUnqualifiedType() == to.getCanonicalType().getUnqualifiedType();
}

static bool functionAllowed(Reporter &reporter, const clang::Stmt *stmt, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*stmt);
  for (int depth = 0; depth < 64; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *fn = node.get<clang::FunctionDecl>()) {
      if (fn->getDeclName().isEmpty()) {
        return false;
      }
      const std::string name = fn->getDeclName().getAsString();
      return !name.empty() && reporter.allows("ss.conv.no-int-to-enum", name.c_str());
    }
  }
  return false;
}

} // namespace

void NoIntToEnumCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(castExpr().bind("cast"), this);
}

void NoIntToEnumCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<CastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.Context || !result.SourceManager) {
    return;
  }
  const QualType to = cast->getType();
  if (!to->isEnumeralType() || cast->isTypeDependent()) {
    return;
  }
  const Expr *src = cast->getSubExpr()->IgnoreParenImpCasts();
  if (!src || src->isTypeDependent()) {
    return;
  }
  if (const auto *ref = llvm::dyn_cast<clang::DeclRefExpr>(src)) {
    if (llvm::isa<clang::EnumConstantDecl>(ref->getDecl())) {
      return;
    }
  }
  if (sameEnum(src->getType(), to)) {
    return;
  }
  if (!src->getType()->isIntegerType() && !src->getType()->isEnumeralType()) {
    return;
  }
  if (functionAllowed(reporter, cast, *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-int-to-enum",
                "do not convert an integer to an enum");
}

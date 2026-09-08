#include "siliscope/StringConst.h"

#include "siliscope/Report.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::Expr;
using clang::ExprWithCleanups;
using clang::ImplicitCastExpr;
using clang::ParenExpr;
using clang::PointerType;
using clang::QualType;
using clang::StringLiteral;
using clang::ast_matchers::stringLiteral;

namespace {

static bool isNonConstCharPointer(QualType t) {
  if (t.isNull()) {
    return false;
  }
  t = t.getCanonicalType();
  const PointerType *p = t->getAs<PointerType>();
  if (!p) {
    return false;
  }
  const QualType e = p->getPointeeType();
  return e->isAnyCharacterType() && !e.isConstQualified();
}

static const Expr *decayedUse(const StringLiteral *lit, clang::ASTContext &ctx) {
  const Expr *e = lit;
  for (int i = 0; i < 64; ++i) {
    const auto parents = ctx.getParents(*e);
    if (parents.empty()) {
      return e;
    }
    if (const auto *p = parents[0].get<ParenExpr>()) {
      e = p;
      continue;
    }
    if (const auto *c = parents[0].get<ImplicitCastExpr>()) {
      e = c;
      continue;
    }
    if (const auto *c = parents[0].get<ExprWithCleanups>()) {
      e = c;
      continue;
    }
    return e;
  }
  return e;
}

} // namespace

void StringConstCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(stringLiteral().bind("str"), this);
}

void StringConstCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *lit = result.Nodes.getNodeAs<StringLiteral>("str");
  if (!lit || !result.Context || !result.SourceManager) {
    return;
  }
  const Expr *e = decayedUse(lit, *result.Context);
  if (!e || !isNonConstCharPointer(e->getType())) {
    return;
  }
  reporter.emit(*result.SourceManager,
                lit->getBeginLoc(),
                "ss.expr.string-const",
                "string literal must bind to const char *");
}

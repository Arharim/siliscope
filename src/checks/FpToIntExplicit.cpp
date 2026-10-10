#include "siliscope/FpToIntExplicit.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CastExpr;
using clang::QualType;
using clang::ast_matchers::castExpr;

namespace {

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
      return !name.empty() && reporter.allows("ss.conv.fp-to-int-explicit", name.c_str());
    }
  }
  return false;
}

} // namespace

void FpToIntExplicitCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(castExpr().bind("cast"), this);
}

void FpToIntExplicitCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<CastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.Context || !result.SourceManager) {
    return;
  }
  if (cast->isTypeDependent() || cast->getSubExpr()->isTypeDependent()) {
    return;
  }
  const QualType from = cast->getSubExpr()->IgnoreParenImpCasts()->getType();
  const QualType to = cast->getType();
  if (!from->isRealFloatingType() || !to->isIntegerType() || to->isBooleanType()) {
    return;
  }
  if (functionAllowed(reporter, cast, *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.fp-to-int-explicit",
                "convert a float to an integer in a named helper");
}

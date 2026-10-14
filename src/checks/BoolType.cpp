#include "siliscope/BoolType.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::ExplicitCastExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::ast_matchers::explicitCastExpr;

namespace {

constexpr const char *kId = "ss.type.bool";
constexpr const char *kMsg = "use a comparison instead of a cast to bool";

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

void BoolTypeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(explicitCastExpr().bind("cast"), this);
}

void BoolTypeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<ExplicitCastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.Context || !result.SourceManager) {
    return;
  }
  if (!cast->getType()->isBooleanType() || cast->getType()->isDependentType()) {
    return;
  }
  const Expr *source = cast->getSubExpr()->IgnoreParenImpCasts();
  if (!source || source->isTypeDependent() || source->getType()->isBooleanType()) {
    return;
  }
  if (allowed(cast, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, cast->getBeginLoc(), kId, kMsg);
}

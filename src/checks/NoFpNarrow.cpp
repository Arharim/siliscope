#include "siliscope/NoFpNarrow.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::ImplicitCastExpr;
using clang::QualType;
using clang::ast_matchers::hasCastKind;
using clang::ast_matchers::implicitCastExpr;

void NoFpNarrowCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(implicitCastExpr(hasCastKind(clang::CK_FloatingCast)).bind("cast"), this);
}

void NoFpNarrowCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<ImplicitCastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.Context || !result.SourceManager) {
    return;
  }
  if (cast->isTypeDependent()) {
    return;
  }
  const QualType from = cast->getSubExpr()->getType();
  const QualType to = cast->getType();
  if (!from->isRealFloatingType() || !to->isRealFloatingType()) {
    return;
  }
  if (result.Context->getTypeSize(to) >= result.Context->getTypeSize(from)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-fp-narrow",
                "do not drop floating precision implicitly");
}

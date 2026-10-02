#include "siliscope/NoCvAway.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CastExpr;
using clang::QualType;
using clang::ast_matchers::castExpr;

namespace {

static bool isPointerOrRef(QualType t) {
  return !t.isNull() && (t->isPointerType() || t->isReferenceType());
}

static bool losesPointedCv(QualType from, QualType to) {
  while (isPointerOrRef(from) && isPointerOrRef(to)) {
    const QualType fp = from->getPointeeType();
    const QualType tp = to->getPointeeType();
    if ((fp.isConstQualified() && !tp.isConstQualified()) ||
        (fp.isVolatileQualified() && !tp.isVolatileQualified())) {
      return true;
    }
    from = fp;
    to = tp;
  }
  return false;
}

} // namespace

void NoCvAwayCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(castExpr().bind("cast"), this);
}

void NoCvAwayCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<CastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.SourceManager) {
    return;
  }
  const QualType from = cast->getSubExpr()->getType();
  const QualType to = cast->getType();
  if (!losesPointedCv(from, to)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-cv-away",
                "cast removes const or volatile from the pointed-to type");
}

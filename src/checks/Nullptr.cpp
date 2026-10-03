#include "siliscope/Nullptr.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXNullPtrLiteralExpr;
using clang::ExplicitCastExpr;
using clang::Expr;
using clang::ImplicitCastExpr;
using clang::ast_matchers::hasCastKind;
using clang::ast_matchers::implicitCastExpr;

namespace {

// nullptr is the only null pointer constant this rule accepts, including
// when it is wrapped in a named cast. NULL from a system header spells as
// the replacement token in that header; report the macro use instead.
bool writtenAsNullptr(const Expr *e) {
  while (e) {
    e = e->IgnoreParenImpCasts();
    if (llvm::isa<CXXNullPtrLiteralExpr>(e)) {
      return true;
    }
    const auto *cast = llvm::dyn_cast<ExplicitCastExpr>(e);
    if (!cast) {
      return false;
    }
    e = cast->getSubExpr();
  }
  return false;
}

clang::SourceLocation reportLoc(const clang::SourceManager &sm, clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return loc;
  }
  const clang::SourceLocation spelled = sm.getSpellingLoc(loc);
  if (spelled.isValid() && sm.isInSystemHeader(spelled)) {
    const clang::SourceLocation expanded = sm.getExpansionLoc(loc);
    if (expanded.isValid()) {
      return expanded;
    }
  }
  return loc;
}

} // namespace

void NullptrCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(implicitCastExpr(hasCastKind(clang::CK_NullToPointer)).bind("cast"), this);
  finder.addMatcher(implicitCastExpr(hasCastKind(clang::CK_NullToMemberPointer)).bind("cast"),
                    this);
}

void NullptrCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  // A null pointer constant written as 0 is normal C. strict still enables
  // this rule while compiling a C translation unit.
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *cast = result.Nodes.getNodeAs<ImplicitCastExpr>("cast");
  if (!cast || !cast->getSubExpr() || writtenAsNullptr(cast->getSubExpr())) {
    return;
  }
  reporter.emit(*result.SourceManager,
                reportLoc(*result.SourceManager, cast->getSubExpr()->getExprLoc()),
                "ss.cpp.nullptr",
                "use nullptr");
}

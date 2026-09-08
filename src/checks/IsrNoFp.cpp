#include "siliscope/IsrNoFp.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::Expr;
using clang::FunctionDecl;
using clang::ImplicitCastExpr;
using clang::ParenExpr;
using clang::QualType;
using clang::UnaryExprOrTypeTraitExpr;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

static bool isFpType(QualType t) {
  return !t.isNull() && t->isRealFloatingType();
}

class FindFp : public clang::RecursiveASTVisitor<FindFp> {
public:
  clang::SourceLocation at;

  bool VisitVarDecl(VarDecl *d) {
    if (at.isValid() || !d) {
      return true;
    }
    if (isFpType(d->getType())) {
      at = d->getLocation();
      return false;
    }
    return true;
  }

  bool VisitExpr(Expr *e) {
    if (at.isValid() || !e) {
      return true;
    }
    if (llvm::isa<ImplicitCastExpr>(e) || llvm::isa<ParenExpr>(e)) {
      return true;
    }
    if (isFpType(e->getType())) {
      at = e->getBeginLoc();
      return false;
    }
    return true;
  }

  bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) { return true; }
};

} // namespace

void IsrNoFpCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void IsrNoFpCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || !isIsr(fn)) {
    return;
  }
  FindFp v;
  for (const auto *p : fn->parameters()) {
    if (p && isFpType(p->getType())) {
      reporter.emit(*result.SourceManager,
                    p->getLocation(),
                    "ss.emb.isr-no-fp",
                    "do not use floating point in an ISR");
      return;
    }
  }
  v.TraverseStmt(fn->getBody());
  if (v.at.isInvalid()) {
    return;
  }
  reporter.emit(
      *result.SourceManager, v.at, "ss.emb.isr-no-fp", "do not use floating point in an ISR");
}

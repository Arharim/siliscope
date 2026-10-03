#include "siliscope/NoMoveConst.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::Decl;
using clang::Expr;
using clang::FunctionDecl;
using clang::QualType;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasName;

namespace {

bool inStd(const Decl *d) {
  if (!d) {
    return false;
  }
  const clang::DeclContext *dc = d->getDeclContext();
  while (dc) {
    if (const auto *ns = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
      if (ns->isStdNamespace()) {
        return true;
      }
    }
    dc = dc->getParent();
  }
  return false;
}

bool constObject(const Expr *arg) {
  if (!arg) {
    return false;
  }
  QualType t = arg->IgnoreParenImpCasts()->getType();
  if (t->isReferenceType()) {
    t = t.getNonReferenceType();
  }
  return !t.isNull() && t.isConstQualified();
}

} // namespace

void NoMoveConstCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(callee(functionDecl(hasName("move")))).bind("call"), this);
}

void NoMoveConstCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  if (!call || call->getNumArgs() < 1) {
    return;
  }
  const FunctionDecl *calleeFn = call->getDirectCallee();
  // std::move on a const object selects the copy constructor.
  if (!calleeFn || !inStd(calleeFn) || !constObject(call->getArg(0))) {
    return;
  }
  reporter.emit(*result.SourceManager,
                call->getBeginLoc(),
                "ss.cpp.no-move-const",
                "do not std::move a const object");
}

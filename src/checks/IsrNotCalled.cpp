#include "siliscope/IsrNotCalled.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/StringRef.h"

using clang::CallExpr;
using clang::FunctionDecl;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;

static bool isIsr(const FunctionDecl *fn) {
  if (!fn) {
    return false;
  }
  fn = fn->getCanonicalDecl();
  if (fn->hasAttrs()) {
    for (const auto *a : fn->attrs()) {
      if (!a) {
        continue;
      }
      const llvm::StringRef sp = a->getSpelling();
      if (sp.contains_insensitive("interrupt")) {
        return true;
      }
    }
  }
  if (!fn->getIdentifier()) {
    return false;
  }
  const llvm::StringRef n = fn->getName();
  return n.ends_with("_IRQHandler") || n.ends_with("_isr");
}

void IsrNotCalledCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(callee(functionDecl())).bind("call"), this);
}

void IsrNotCalledCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *c = result.Nodes.getNodeAs<CallExpr>("call");
  if (!c) {
    return;
  }
  const FunctionDecl *callee_fn = c->getDirectCallee();
  if (!isIsr(callee_fn)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                c->getBeginLoc(),
                "ss.emb.isr-not-called",
                "do not call an ISR like a normal function");
}

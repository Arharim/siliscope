#include "siliscope/IsrNotCalled.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::FunctionDecl;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;

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

#include "siliscope/Noreturn.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/CFG.h"

using clang::CFG;
using clang::CFGBlock;
using clang::FunctionDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

static bool canReturnToCaller(const CFG &cfg) {
  for (const CFGBlock *pred : cfg.getExit().preds()) {
    if (!pred) {
      continue;
    }
    if (!pred->hasNoReturnElement()) {
      return true;
    }
  }
  return false;
}

void NoreturnCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoreturnCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || !fn->getBody() || !fn->isNoReturn()) {
    return;
  }
  if (!fn->getReturnType()->isVoidType()) {
    reporter.emit(*result.SourceManager,
                  fn->getLocation(),
                  "ss.fn.noreturn-does-not-return",
                  "noreturn function must have void result");
  }
  std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, CFG::BuildOptions());
  if (!cfg || !canReturnToCaller(*cfg)) {
    return;
  }
  clang::SourceLocation at = fn->getBody()->getEndLoc();
  if (at.isInvalid()) {
    at = fn->getLocation();
  }
  reporter.emit(*result.SourceManager,
                at,
                "ss.fn.noreturn-does-not-return",
                "noreturn function must not return");
}

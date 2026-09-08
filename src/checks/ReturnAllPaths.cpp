#include "siliscope/ReturnAllPaths.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/CFG.h"

using clang::CFG;
using clang::CFGBlock;
using clang::CFGStmt;
using clang::FunctionDecl;
using clang::ReturnStmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::returns;
using clang::ast_matchers::unless;
using clang::ast_matchers::voidType;

static bool predFallsOff(const CFGBlock &b) {
  if (b.hasNoReturnElement()) {
    return false;
  }
  for (auto it = b.rbegin(), end = b.rend(); it != end; ++it) {
    if (std::optional<CFGStmt> cs = it->getAs<CFGStmt>()) {
      return !llvm::isa<ReturnStmt>(cs->getStmt());
    }
  }
  return true;
}

void ReturnAllPathsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(
      functionDecl(isDefinition(), unless(isImplicit()), unless(returns(voidType()))).bind("fn"),
      this);
}

void ReturnAllPathsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || !fn->getBody()) {
    return;
  }
  if (fn->hasAttr<clang::NoReturnAttr>()) {
    return;
  }
  std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, CFG::BuildOptions());
  if (!cfg) {
    return;
  }
  bool missing = false;
  for (const CFGBlock *pred : cfg->getExit().preds()) {
    if (!pred) {
      continue;
    }
    if (predFallsOff(*pred)) {
      missing = true;
      break;
    }
  }
  if (!missing) {
    return;
  }
  clang::SourceLocation at = fn->getBody()->getEndLoc();
  if (at.isInvalid()) {
    at = fn->getLocation();
  }
  reporter.emit(*result.SourceManager,
                at,
                "ss.fn.return-all-paths",
                "non-void function must return a value on every path");
}

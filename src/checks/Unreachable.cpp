#include "siliscope/Unreachable.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/BitVector.h"

using clang::CallExpr;
using clang::CFG;
using clang::CFGBlock;
using clang::CFGStmt;
using clang::FunctionDecl;
using clang::Stmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

static bool isUnreachableMarker(const Stmt *s) {
  const auto *call = llvm::dyn_cast<CallExpr>(s);
  if (!call) {
    return false;
  }
  const FunctionDecl *callee = call->getDirectCallee();
  return callee && callee->getBuiltinID() == clang::Builtin::BI__builtin_unreachable;
}

static const Stmt *firstStmt(const CFGBlock &b) {
  for (const auto &el : b) {
    if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
      const Stmt *s = cs->getStmt();
      if (s && !isUnreachableMarker(s)) {
        return s;
      }
    }
  }
  return nullptr;
}

void UnreachableCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void UnreachableCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || !fn->getBody()) {
    return;
  }
  std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, CFG::BuildOptions());
  if (!cfg) {
    return;
  }
  llvm::BitVector live(cfg->getNumBlockIDs());
  clang::reachable_code::ScanReachableFromBlock(&cfg->getEntry(), live);
  for (const CFGBlock *b : *cfg) {
    if (!b || live[b->getBlockID()] || b == &cfg->getEntry() || b == &cfg->getExit()) {
      continue;
    }
    const Stmt *s = firstStmt(*b);
    if (!s) {
      continue;
    }
    reporter.emit(
        *result.SourceManager, s->getBeginLoc(), "ss.ctrl.unreachable", "this code is unreachable");
  }
}

#include "siliscope/CsBalanced.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/StringRef.h"

#include <vector>

using clang::CallExpr;
using clang::CFG;
using clang::CFGBlock;
using clang::CFGStmt;
using clang::FunctionDecl;
using clang::ReturnStmt;
using clang::Stmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

enum CSOp { None, Enter, Leave };

static CSOp classify(const FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return None;
  }
  const llvm::StringRef n = fn->getName();
  if (n.contains("ENTER_CRITICAL") || n.contains("EnterCritical") || n.ends_with("disable_irq") ||
      n == "__disable_fault_irq" || n == "irq_lock") {
    return Enter;
  }
  if (n.contains("EXIT_CRITICAL") || n.contains("ExitCritical") || n.ends_with("enable_irq") ||
      n == "__enable_fault_irq" || n == "irq_unlock") {
    return Leave;
  }
  return None;
}

static void collectCalls(const Stmt *s, std::vector<const CallExpr *> &out) {
  if (!s) {
    return;
  }
  for (const Stmt *ch : s->children()) {
    collectCalls(ch, out);
  }
  if (const auto *c = llvm::dyn_cast<CallExpr>(s)) {
    out.push_back(c);
  }
}

static clang::SourceLocation firstLoc(const CFGBlock &b, const FunctionDecl &fn) {
  for (const auto &el : b) {
    if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
      if (cs->getStmt()) {
        return cs->getStmt()->getBeginLoc();
      }
    }
  }
  if (const Stmt *t = b.getTerminatorStmt()) {
    return t->getBeginLoc();
  }
  if (fn.getBody()) {
    return fn.getBody()->getEndLoc();
  }
  return fn.getLocation();
}

static clang::SourceLocation exitLoc(const CFGBlock &b, const FunctionDecl &fn) {
  for (auto it = b.rbegin(), end = b.rend(); it != end; ++it) {
    if (std::optional<CFGStmt> cs = it->getAs<CFGStmt>()) {
      if (const auto *ret = llvm::dyn_cast<ReturnStmt>(cs->getStmt())) {
        return ret->getBeginLoc();
      }
    }
  }
  if (fn.getBody()) {
    return fn.getBody()->getEndLoc();
  }
  return fn.getLocation();
}

} // namespace

void CsBalancedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void CsBalancedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || !fn->getBody() || !result.SourceManager) {
    return;
  }
  std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, CFG::BuildOptions());
  if (!cfg) {
    return;
  }

  const unsigned n = cfg->getNumBlockIDs();
  std::vector<int> in_depth(n, -1);
  std::vector<char> join_reported(n, 0);
  std::vector<const CFGBlock *> work;

  const CFGBlock &entry = cfg->getEntry();
  in_depth[entry.getBlockID()] = 0;
  work.push_back(&entry);

  const CFGBlock *exit = &cfg->getExit();
  auto apply = [&](int d, const CFGBlock &b) -> int {
    for (const auto &el : b) {
      if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
        std::vector<const CallExpr *> calls;
        collectCalls(cs->getStmt(), calls);
        for (const CallExpr *c : calls) {
          const CSOp op = classify(c->getDirectCallee());
          if (op == Enter) {
            ++d;
          } else if (op == Leave) {
            if (d == 0) {
              reporter.emit(*result.SourceManager,
                            c->getBeginLoc(),
                            "ss.emb.cs-balanced",
                            "critical section leave without a matching enter");
            } else {
              --d;
            }
          }
        }
      }
    }
    return d;
  };

  while (!work.empty()) {
    const CFGBlock *b = work.back();
    work.pop_back();
    int d = apply(in_depth[b->getBlockID()], *b);
    if (b->hasNoReturnElement()) {
      continue;
    }
    for (const CFGBlock *succ : b->succs()) {
      if (!succ) {
        continue;
      }
      if (succ == exit) {
        if (d > 0) {
          reporter.emit(*result.SourceManager,
                        exitLoc(*b, *fn),
                        "ss.emb.cs-balanced",
                        "critical section is not left on this path");
        }
        continue;
      }
      const unsigned id = succ->getBlockID();
      if (in_depth[id] < 0) {
        in_depth[id] = d;
        work.push_back(succ);
      } else if (in_depth[id] != d && !join_reported[id]) {
        join_reported[id] = 1;
        reporter.emit(*result.SourceManager,
                      firstLoc(*succ, *fn),
                      "ss.emb.cs-balanced",
                      "critical section state differs across paths");
      }
    }
  }
}

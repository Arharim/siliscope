#include "siliscope/IrqMaskBalanced.h"

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

enum IrqOp {
  None,
  BlindEnable,
  DisableIrq,
  RestorePrimask,
  DisableFault,
  RestoreFault,
  RaiseBasepri,
  SetBasepri
};

struct Depth {
  int irq = 0;
  int fault = 0;
  int basepri = 0;

  bool operator==(const Depth &o) const {
    return irq == o.irq && fault == o.fault && basepri == o.basepri;
  }
  bool operator!=(const Depth &o) const { return !(*this == o); }
  bool held() const { return irq > 0 || fault > 0 || basepri > 0; }
};

static IrqOp classify(const FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return None;
  }
  const llvm::StringRef n = fn->getName();
  if (n.ends_with("enable_irq") || n == "__enable_fault_irq" || n == "cpsie") {
    return BlindEnable;
  }
  if (n.ends_with("disable_irq") || n == "cpsid") {
    return DisableIrq;
  }
  if (n == "__disable_fault_irq") {
    return DisableFault;
  }
  if (n == "__set_PRIMASK") {
    return RestorePrimask;
  }
  if (n == "__set_FAULTMASK") {
    return RestoreFault;
  }
  if (n == "__set_BASEPRI_MAX") {
    return RaiseBasepri;
  }
  if (n == "__set_BASEPRI") {
    return SetBasepri;
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

void IrqMaskBalancedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void IrqMaskBalancedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || !fn->getBody() || !result.SourceManager) {
    return;
  }
  std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, CFG::BuildOptions());
  if (!cfg) {
    return;
  }

  const unsigned n = cfg->getNumBlockIDs();
  std::vector<char> seen(n, 0);
  std::vector<Depth> in_depth(n);
  std::vector<char> join_reported(n, 0);
  std::vector<const CFGBlock *> work;

  const CFGBlock &entry = cfg->getEntry();
  seen[entry.getBlockID()] = 1;
  work.push_back(&entry);

  const CFGBlock *exit = &cfg->getExit();
  auto apply = [&](Depth d, const CFGBlock &b) -> Depth {
    for (const auto &el : b) {
      if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
        std::vector<const CallExpr *> calls;
        collectCalls(cs->getStmt(), calls);
        for (const CallExpr *c : calls) {
          switch (classify(c->getDirectCallee())) {
          case BlindEnable:
            reporter.emit(*result.SourceManager,
                          c->getBeginLoc(),
                          "ss.emb.irq-mask-balanced",
                          "do not blindly re-enable interrupts; restore the saved mask");
            break;
          case DisableIrq:
            ++d.irq;
            break;
          case RestorePrimask:
            if (d.irq > 0) {
              --d.irq;
            }
            break;
          case DisableFault:
            ++d.fault;
            break;
          case RestoreFault:
            if (d.fault > 0) {
              --d.fault;
            }
            break;
          case RaiseBasepri:
            ++d.basepri;
            break;
          case SetBasepri:
            if (d.basepri > 0) {
              --d.basepri;
            } else {
              ++d.basepri;
            }
            break;
          case None:
            break;
          }
        }
      }
    }
    return d;
  };

  while (!work.empty()) {
    const CFGBlock *b = work.back();
    work.pop_back();
    const Depth d = apply(in_depth[b->getBlockID()], *b);
    if (b->hasNoReturnElement()) {
      continue;
    }
    for (const CFGBlock *succ : b->succs()) {
      if (!succ) {
        continue;
      }
      if (succ == exit) {
        if (d.held()) {
          reporter.emit(*result.SourceManager,
                        exitLoc(*b, *fn),
                        "ss.emb.irq-mask-balanced",
                        "interrupt mask is not restored on this path");
        }
        continue;
      }
      const unsigned id = succ->getBlockID();
      if (!seen[id]) {
        seen[id] = 1;
        in_depth[id] = d;
        work.push_back(succ);
      } else if (in_depth[id] != d && !join_reported[id]) {
        join_reported[id] = 1;
        reporter.emit(*result.SourceManager,
                      firstLoc(*succ, *fn),
                      "ss.emb.irq-mask-balanced",
                      "interrupt mask state differs across paths");
      }
    }
  }
}

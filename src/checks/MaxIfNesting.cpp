#include "siliscope/MaxIfNesting.h"

#include "siliscope/Report.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::ASTContext;
using clang::DynTypedNode;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::Stmt;
using clang::ast_matchers::ifStmt;

namespace {

constexpr const char *kId = "ss.ctrl.max-if-nesting";
constexpr const char *kMsg = "do not nest if-else more than two levels";

// An else-if chain stays one level. Nesting is an if inside a branch body.
unsigned nestDepth(const IfStmt *stmt, ASTContext &ctx) {
  unsigned depth = 1;
  const Stmt *cur = stmt;
  for (int step = 0; step < 64 && cur; ++step) {
    const auto parents = ctx.getParents(*cur);
    if (parents.empty()) {
      break;
    }
    const Stmt *parent = parents[0].get<Stmt>();
    if (!parent) {
      break;
    }
    if (const auto *outer = llvm::dyn_cast<IfStmt>(parent)) {
      if (llvm::isa<IfStmt>(cur) && outer->getElse() == cur) {
        cur = outer;
        continue;
      }
      ++depth;
    }
    cur = parent;
  }
  return depth;
}

bool allowed(const IfStmt *stmt, ASTContext &ctx, Reporter &reporter) {
  DynTypedNode node = DynTypedNode::create(*stmt);
  for (int step = 0; step < 32; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      if (fn->getDeclName().isEmpty()) {
        return false;
      }
      const std::string name = fn->getDeclName().getAsString();
      return !name.empty() && reporter.allows(kId, name.c_str());
    }
  }
  return false;
}

} // namespace

void MaxIfNestingCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(ifStmt().bind("if"), this);
}

void MaxIfNestingCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *stmt = result.Nodes.getNodeAs<IfStmt>("if");
  if (!stmt || !result.Context || !result.SourceManager) {
    return;
  }
  if (nestDepth(stmt, *result.Context) <= 2 || allowed(stmt, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, stmt->getIfLoc(), kId, kMsg);
}

#include "siliscope/CheckReturn.h"

#include "siliscope/Report.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::CallExpr;
using clang::CastExpr;
using clang::CompoundStmt;
using clang::ConstantExpr;
using clang::CXXOperatorCallExpr;
using clang::ExprWithCleanups;
using clang::FullExpr;
using clang::ParenExpr;
using clang::Stmt;
using clang::ast_matchers::callExpr;

namespace {

static bool isAssignmentOrIncOp(const CallExpr *c) {
  const auto *op = llvm::dyn_cast<CXXOperatorCallExpr>(c);
  if (!op) {
    return false;
  }
  if (op->isAssignmentOp()) {
    return true;
  }
  const auto k = op->getOperator();
  return k == clang::OO_PlusPlus || k == clang::OO_MinusMinus;
}

static bool isDiscarded(const CallExpr *c, clang::ASTContext &ctx) {
  const Stmt *cur = c;
  for (int i = 0; i < 64; ++i) {
    const auto parents = ctx.getParents(*cur);
    if (parents.empty()) {
      return false;
    }
    if (const auto *ce = parents[0].get<CastExpr>()) {
      if (ce->getCastKind() == clang::CK_ToVoid) {
        return false;
      }
      cur = ce;
      continue;
    }
    if (const auto *e = parents[0].get<ParenExpr>()) {
      cur = e;
      continue;
    }
    if (const auto *e = parents[0].get<ExprWithCleanups>()) {
      cur = e;
      continue;
    }
    if (const auto *e = parents[0].get<FullExpr>()) {
      cur = e;
      continue;
    }
    if (const auto *e = parents[0].get<ConstantExpr>()) {
      cur = e;
      continue;
    }
    if (const auto *bo = parents[0].get<BinaryOperator>()) {
      if (bo->getOpcode() == clang::BO_Comma && bo->getLHS() == cur) {
        return true;
      }
      return false;
    }
    if (parents[0].get<CompoundStmt>() || parents[0].get<clang::CaseStmt>() ||
        parents[0].get<clang::DefaultStmt>() || parents[0].get<clang::LabelStmt>() ||
        parents[0].get<clang::AttributedStmt>()) {
      return true;
    }
    return false;
  }
  return false;
}

} // namespace

void CheckReturnCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr().bind("call"), this);
}

void CheckReturnCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *c = result.Nodes.getNodeAs<CallExpr>("call");
  if (!c || !result.Context || !result.SourceManager) {
    return;
  }
  if (c->isTypeDependent() || c->getType()->isVoidType()) {
    return;
  }
  if (isAssignmentOrIncOp(c)) {
    return;
  }
  if (!isDiscarded(c, *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                c->getBeginLoc(),
                "ss.fn.check-return",
                "use or explicitly discard this return value");
}

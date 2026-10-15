#include "siliscope/NamedLoopLimit.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::ASTContext;
using clang::BinaryOperator;
using clang::DoStmt;
using clang::DynTypedNode;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IntegerLiteral;
using clang::SourceManager;
using clang::Stmt;
using clang::UnaryOperator;
using clang::WhileStmt;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::whileStmt;

namespace {

constexpr const char *kId = "ss.ctrl.named-loop-limit";
constexpr const char *kMsg = "name this loop limit";

const Expr *strip(const Expr *expr) {
  while (expr) {
    if (const auto *cast = llvm::dyn_cast<clang::CastExpr>(expr)) {
      if (!llvm::isa<clang::ExplicitCastExpr>(cast)) {
        expr = cast->getSubExpr();
        continue;
      }
    }
    if (const auto *paren = llvm::dyn_cast<clang::ParenExpr>(expr)) {
      expr = paren->getSubExpr();
      continue;
    }
    break;
  }
  return expr;
}

bool exempt(const IntegerLiteral *lit) {
  const llvm::APInt &value = lit->getValue();
  return value.isZero() || value.isOne();
}

bool bareLimit(const Expr *operand, const SourceManager &sm, const IntegerLiteral *&out) {
  operand = strip(operand);
  if (!operand) {
    return false;
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(operand)) {
    if (unary->getOpcode() == clang::UO_Minus) {
      operand = strip(unary->getSubExpr());
    }
  }
  const auto *lit = llvm::dyn_cast_or_null<IntegerLiteral>(operand);
  if (!lit || exempt(lit) || sm.isMacroBodyExpansion(lit->getBeginLoc())) {
    return false;
  }
  out = lit;
  return true;
}

bool allowed(const Stmt *stmt, ASTContext &ctx, Reporter &reporter) {
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

class BoundWalk : public clang::RecursiveASTVisitor<BoundWalk> {
public:
  BoundWalk(const SourceManager &sm, Reporter &reporter) : sm(sm), reporter(reporter) {}

  bool VisitBinaryOperator(BinaryOperator *cmp) {
    if (!cmp || !cmp->isComparisonOp()) {
      return true;
    }
    const IntegerLiteral *lit = nullptr;
    if (bareLimit(cmp->getLHS(), sm, lit) || bareLimit(cmp->getRHS(), sm, lit)) {
      reporter.emit(sm, lit->getBeginLoc(), kId, kMsg);
    }
    return true;
  }

private:
  const SourceManager &sm;
  Reporter &reporter;
};

void checkCond(const Stmt *loop,
               const Expr *cond,
               Reporter &reporter,
               const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!loop || !cond || !result.Context || !result.SourceManager) {
    return;
  }
  if (allowed(loop, *result.Context, reporter)) {
    return;
  }
  BoundWalk walk(*result.SourceManager, reporter);
  walk.TraverseStmt(const_cast<Expr *>(cond));
}

} // namespace

void NamedLoopLimitCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(forStmt().bind("for"), this);
  finder.addMatcher(whileStmt().bind("while"), this);
  finder.addMatcher(doStmt().bind("do"), this);
}

void NamedLoopLimitCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (const auto *loop = result.Nodes.getNodeAs<ForStmt>("for")) {
    checkCond(loop, loop->getCond(), reporter, result);
    return;
  }
  if (const auto *loop = result.Nodes.getNodeAs<WhileStmt>("while")) {
    checkCond(loop, loop->getCond(), reporter, result);
    return;
  }
  if (const auto *loop = result.Nodes.getNodeAs<DoStmt>("do")) {
    checkCond(loop, loop->getCond(), reporter, result);
  }
}

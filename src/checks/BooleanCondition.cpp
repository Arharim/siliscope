#include "siliscope/BooleanCondition.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::BinaryOperator;
using clang::DoStmt;
using clang::ExplicitCastExpr;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::UnaryOperator;
using clang::WhileStmt;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::ifStmt;
using clang::ast_matchers::whileStmt;

namespace {

constexpr const char *kId = "ss.ctrl.boolean-condition";

bool accepted(const Expr *cond) {
  if (!cond) {
    return true;
  }
  cond = cond->IgnoreParenImpCasts();
  if (!cond || cond->getType()->isBooleanType()) {
    return true;
  }
  if (const auto *cast = llvm::dyn_cast<ExplicitCastExpr>(cond)) {
    return cast->getType()->isBooleanType();
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(cond)) {
    return unary->getOpcode() == clang::UO_LNot;
  }
  if (const auto *binary = llvm::dyn_cast<BinaryOperator>(cond)) {
    return binary->isComparisonOp() || binary->isLogicalOp();
  }
  return false;
}

const FunctionDecl *enclosing(const clang::Stmt *s, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*s);
  for (int depth = 0; depth < 32; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      return fn;
    }
  }
  return nullptr;
}

bool allowed(const clang::Stmt *s, clang::ASTContext &ctx, Reporter &reporter) {
  const FunctionDecl *fn = enclosing(s, ctx);
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

void check(const clang::Stmt *stmt,
           const Expr *cond,
           Reporter &reporter,
           const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!stmt || !cond || !result.SourceManager || !result.Context || accepted(cond)) {
    return;
  }
  if (allowed(stmt, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cond->getExprLoc(),
                kId,
                "condition should be a boolean or a comparison");
}

} // namespace

void BooleanConditionCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(ifStmt().bind("if"), this);
  finder.addMatcher(whileStmt().bind("while"), this);
  finder.addMatcher(doStmt().bind("do"), this);
  finder.addMatcher(forStmt().bind("for"), this);
}

void BooleanConditionCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (const auto *stmt = result.Nodes.getNodeAs<IfStmt>("if")) {
    check(stmt, stmt->getCond(), reporter, result);
    return;
  }
  if (const auto *stmt = result.Nodes.getNodeAs<WhileStmt>("while")) {
    check(stmt, stmt->getCond(), reporter, result);
    return;
  }
  if (const auto *stmt = result.Nodes.getNodeAs<DoStmt>("do")) {
    check(stmt, stmt->getCond(), reporter, result);
    return;
  }
  if (const auto *stmt = result.Nodes.getNodeAs<ForStmt>("for")) {
    check(stmt, stmt->getCond(), reporter, result);
  }
}

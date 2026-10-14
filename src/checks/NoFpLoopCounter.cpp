#include "siliscope/NoFpLoopCounter.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <set>
#include <string>

using clang::BinaryOperator;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::Stmt;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::WhileStmt;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::whileStmt;

namespace {

constexpr const char *kId = "ss.ctrl.no-fp-loop-counter";

bool isFloat(const Expr *e) {
  return e && !e->getType().isNull() && e->getType()->isRealFloatingType();
}

const VarDecl *floatVar(const Expr *e) {
  if (!e) {
    return nullptr;
  }
  const auto *ref = llvm::dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
  if (!ref) {
    return nullptr;
  }
  const auto *vd = llvm::dyn_cast<VarDecl>(ref->getDecl());
  if (!vd || vd->getType().isNull() || !vd->getType()->isRealFloatingType()) {
    return nullptr;
  }
  return vd->getCanonicalDecl();
}

void mention(const Expr *e, std::set<const VarDecl *> &vars) {
  if (!e) {
    return;
  }
  e = e->IgnoreParenImpCasts();
  if (const VarDecl *vd = floatVar(e)) {
    vars.insert(vd);
    return;
  }
  if (const auto *binary = llvm::dyn_cast<BinaryOperator>(e)) {
    if (binary->isComparisonOp() || binary->isLogicalOp()) {
      mention(binary->getLHS(), vars);
      mention(binary->getRHS(), vars);
    }
  }
}

void modified(const Stmt *s, std::set<const VarDecl *> &vars) {
  if (!s || llvm::isa<clang::LambdaExpr>(s)) {
    return;
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(s)) {
    if (unary->isIncrementDecrementOp()) {
      if (const VarDecl *vd = floatVar(unary->getSubExpr())) {
        vars.insert(vd);
      }
    }
  } else if (const auto *binary = llvm::dyn_cast<BinaryOperator>(s)) {
    if (binary->isAssignmentOp() || binary->isCompoundAssignmentOp()) {
      if (const VarDecl *vd = floatVar(binary->getLHS())) {
        vars.insert(vd);
      }
    }
  }
  for (const Stmt *child : s->children()) {
    modified(child, vars);
  }
}

bool storesFloat(const Stmt *s) {
  if (!s || llvm::isa<clang::LambdaExpr>(s)) {
    return false;
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(s)) {
    if (unary->isIncrementDecrementOp() && isFloat(unary->getSubExpr()->IgnoreParenImpCasts())) {
      return true;
    }
  } else if (const auto *binary = llvm::dyn_cast<BinaryOperator>(s)) {
    if ((binary->isAssignmentOp() || binary->isCompoundAssignmentOp()) &&
        isFloat(binary->getLHS()->IgnoreParenImpCasts())) {
      return true;
    }
  }
  for (const Stmt *child : s->children()) {
    if (storesFloat(child)) {
      return true;
    }
  }
  return false;
}

bool declaredFloat(const Stmt *init) {
  const auto *stmt = llvm::dyn_cast_or_null<DeclStmt>(init);
  if (!stmt) {
    return false;
  }
  for (const clang::Decl *decl : stmt->decls()) {
    const auto *vd = llvm::dyn_cast<VarDecl>(decl);
    if (vd && !vd->getType().isNull() && vd->getType()->isRealFloatingType()) {
      return true;
    }
  }
  return false;
}

bool conditionStepsFloat(const Expr *cond, const Stmt *body, const Stmt *inc) {
  std::set<const VarDecl *> named;
  mention(cond, named);
  if (named.empty()) {
    return false;
  }
  std::set<const VarDecl *> stepped;
  modified(body, stepped);
  modified(inc, stepped);
  for (const VarDecl *vd : named) {
    if (stepped.count(vd) != 0) {
      return true;
    }
  }
  return false;
}

bool allowed(const Stmt *s, clang::ASTContext &ctx, Reporter &reporter) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*s);
  for (int depth = 0; depth < 32; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    const auto *fn = node.get<FunctionDecl>();
    if (!fn) {
      continue;
    }
    if (fn->getDeclName().isEmpty()) {
      return false;
    }
    const std::string name = fn->getDeclName().getAsString();
    return !name.empty() && reporter.allows(kId, name.c_str());
  }
  return false;
}

void emit(const Stmt *loop,
          clang::SourceLocation loc,
          Reporter &reporter,
          const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context || allowed(loop, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, loc, kId, "a loop counter shall have integer type");
}

} // namespace

void NoFpLoopCounterCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(forStmt().bind("for"), this);
  finder.addMatcher(whileStmt().bind("while"), this);
  finder.addMatcher(doStmt().bind("do"), this);
}

void NoFpLoopCounterCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (const auto *loop = result.Nodes.getNodeAs<ForStmt>("for")) {
    const bool bad = declaredFloat(loop->getInit()) || storesFloat(loop->getInit()) ||
                     storesFloat(loop->getInc()) ||
                     conditionStepsFloat(loop->getCond(), loop->getBody(), loop->getInc());
    if (bad) {
      emit(loop, loop->getForLoc(), reporter, result);
    }
    return;
  }
  if (const auto *loop = result.Nodes.getNodeAs<WhileStmt>("while")) {
    if (conditionStepsFloat(loop->getCond(), loop->getBody(), nullptr)) {
      emit(loop, loop->getWhileLoc(), reporter, result);
    }
    return;
  }
  if (const auto *loop = result.Nodes.getNodeAs<DoStmt>("do")) {
    if (conditionStepsFloat(loop->getCond(), loop->getBody(), nullptr)) {
      emit(loop, loop->getDoLoc(), reporter, result);
    }
  }
}

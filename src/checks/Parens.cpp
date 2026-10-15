#include "siliscope/Parens.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorPrecedence.h"

#include <string>

using clang::ASTContext;
using clang::BinaryOperator;
using clang::DeclRefExpr;
using clang::DynTypedNode;
using clang::Expr;
using clang::FunctionDecl;
using clang::ParenExpr;
using clang::ast_matchers::binaryOperator;

namespace {

constexpr const char *kId = "ss.expr.parens";
constexpr const char *kMixed = "parenthesize this mixed-precedence expression";
constexpr const char *kLogic = "parenthesize this && or || operand";

const Expr *stripImplicit(const Expr *expr) {
  while (expr) {
    if (const auto *cast = llvm::dyn_cast<clang::ImplicitCastExpr>(expr)) {
      expr = cast->getSubExpr();
      continue;
    }
    if (const auto *clean = llvm::dyn_cast<clang::ExprWithCleanups>(expr)) {
      expr = clean->getSubExpr();
      continue;
    }
    if (const auto *mat = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr)) {
      expr = mat->getSubExpr();
      continue;
    }
    break;
  }
  return expr;
}

bool allowed(const Expr *expr, ASTContext &ctx, Reporter &reporter) {
  DynTypedNode node = DynTypedNode::create(*expr);
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

bool interesting(const BinaryOperator *bin) {
  return bin && !bin->isAssignmentOp() && bin->getOpcode() != clang::BO_Comma;
}

// Clang 23 dropped BinaryOperator::getPrecedence. The levels match prec::Level.
clang::prec::Level precedence(clang::BinaryOperator::Opcode op) {
  if (BinaryOperator::isPtrMemOp(op)) {
    return clang::prec::PointerToMember;
  }
  if (BinaryOperator::isMultiplicativeOp(op)) {
    return clang::prec::Multiplicative;
  }
  if (BinaryOperator::isAdditiveOp(op)) {
    return clang::prec::Additive;
  }
  if (BinaryOperator::isShiftOp(op)) {
    return clang::prec::Shift;
  }
  if (op == clang::BO_Cmp) {
    return clang::prec::Spaceship;
  }
  if (BinaryOperator::isRelationalOp(op)) {
    return clang::prec::Relational;
  }
  if (BinaryOperator::isEqualityOp(op)) {
    return clang::prec::Equality;
  }
  if (op == clang::BO_And) {
    return clang::prec::And;
  }
  if (op == clang::BO_Xor) {
    return clang::prec::ExclusiveOr;
  }
  if (op == clang::BO_Or) {
    return clang::prec::InclusiveOr;
  }
  if (op == clang::BO_LAnd) {
    return clang::prec::LogicalAnd;
  }
  if (op == clang::BO_LOr) {
    return clang::prec::LogicalOr;
  }
  if (BinaryOperator::isAssignmentOp(op)) {
    return clang::prec::Assignment;
  }
  if (BinaryOperator::isCommaOp(op)) {
    return clang::prec::Comma;
  }
  return clang::prec::Unknown;
}

} // namespace

void ParensCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("bin"), this);
}

void ParensCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *bin = result.Nodes.getNodeAs<BinaryOperator>("bin");
  if (!bin || !result.Context || !result.SourceManager || !interesting(bin)) {
    return;
  }
  if (allowed(bin, *result.Context, reporter)) {
    return;
  }
  const clang::SourceManager &sm = *result.SourceManager;
  if (bin->isLogicalOp()) {
    for (const Expr *kid : {bin->getLHS(), bin->getRHS()}) {
      const Expr *operand = stripImplicit(kid);
      if (!operand || llvm::isa<ParenExpr>(operand) || llvm::isa<DeclRefExpr>(operand)) {
        continue;
      }
      reporter.emit(sm, operand->getExprLoc(), kId, kLogic);
    }
  }
  for (const Expr *kid : {bin->getLHS(), bin->getRHS()}) {
    const Expr *operand = stripImplicit(kid);
    const auto *inner = llvm::dyn_cast_or_null<BinaryOperator>(operand);
    if (!interesting(inner) || precedence(inner->getOpcode()) == precedence(bin->getOpcode())) {
      continue;
    }
    reporter.emit(sm, bin->getOperatorLoc(), kId, kMixed);
    break;
  }
}

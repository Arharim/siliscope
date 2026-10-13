#include "siliscope/NoPtrArith.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::Expr;
using clang::QualType;
using clang::UnaryOperator;
using clang::ast_matchers::anyOf;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::unaryOperator;

namespace {

bool isInteger(QualType t) {
  return !t.isNull() && !t->isDependentType() && t->isIntegerType();
}

bool isObjectPointer(QualType t) {
  return !t.isNull() && !t->isDependentType() && t->isPointerType();
}

// Pointer minus pointer belongs to ss.ptr.same-array. Subscript syntax is the
// indexing form and is not a binary operator here.
bool pointerAndInteger(QualType lhs, QualType rhs) {
  const bool lp = isObjectPointer(lhs);
  const bool rp = isObjectPointer(rhs);
  if (lp && rp) {
    return false;
  }
  return (lp && isInteger(rhs)) || (rp && isInteger(lhs));
}

bool namesImplicit(const Expr *e) {
  const auto *ref = llvm::dyn_cast_or_null<clang::DeclRefExpr>(e->IgnoreParenImpCasts());
  const auto *vd = ref ? llvm::dyn_cast<clang::VarDecl>(ref->getDecl()) : nullptr;
  return vd && vd->isImplicit();
}

// A range-for over an array is lowered to __end = a + n and ++__begin.
bool rangeForSugar(const Expr *op, clang::ASTContext &ctx) {
  if (const auto *bin = llvm::dyn_cast<clang::BinaryOperator>(op)) {
    if (namesImplicit(bin->getLHS()) || namesImplicit(bin->getRHS())) {
      return true;
    }
  } else if (const auto *un = llvm::dyn_cast<clang::UnaryOperator>(op)) {
    if (namesImplicit(un->getSubExpr())) {
      return true;
    }
  }
  clang::DynTypedNode node = clang::DynTypedNode::create(*op);
  for (int depth = 0; depth < 8; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *vd = node.get<clang::VarDecl>()) {
      return vd->isImplicit();
    }
    if (node.get<clang::CompoundStmt>() || node.get<clang::CXXForRangeStmt>()) {
      return false;
    }
  }
  return false;
}

} // namespace

void NoPtrArithCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator(anyOf(hasOperatorName("+"),
                                         hasOperatorName("-"),
                                         hasOperatorName("+="),
                                         hasOperatorName("-=")))
                        .bind("bin"),
                    this);
  finder.addMatcher(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--"))).bind("un"),
                    this);
}

void NoPtrArithCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context) {
    return;
  }
  if (const auto *bin = result.Nodes.getNodeAs<BinaryOperator>("bin")) {
    const Expr *lhs = bin->getLHS();
    const Expr *rhs = bin->getRHS();
    if (!lhs || !rhs || lhs->isTypeDependent() || rhs->isTypeDependent()) {
      return;
    }
    if (!pointerAndInteger(lhs->getType(), rhs->getType()) || rangeForSugar(bin, *result.Context)) {
      return;
    }
    reporter.emit(*result.SourceManager,
                  bin->getOperatorLoc(),
                  "ss.ptr.no-ptr-arith",
                  "do not do pointer arithmetic; index an array");
    return;
  }
  const auto *un = result.Nodes.getNodeAs<UnaryOperator>("un");
  if (!un || !un->getSubExpr() || un->getSubExpr()->isTypeDependent()) {
    return;
  }
  if (!isObjectPointer(un->getSubExpr()->getType()) || rangeForSugar(un, *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                un->getOperatorLoc(),
                "ss.ptr.no-ptr-arith",
                "do not do pointer arithmetic; index an array");
}

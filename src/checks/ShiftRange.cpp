#include "siliscope/ShiftRange.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <algorithm>

using clang::BinaryOperator;
using clang::Expr;
using clang::QualType;
using clang::ast_matchers::binaryOperator;

namespace {

static bool isShift(clang::BinaryOperator::Opcode op) {
  return op == clang::BO_Shl || op == clang::BO_Shr || op == clang::BO_ShlAssign ||
         op == clang::BO_ShrAssign;
}

static QualType shiftedType(const BinaryOperator *op) {
  if (const auto *ca = llvm::dyn_cast<clang::CompoundAssignOperator>(op)) {
    return ca->getComputationLHSType();
  }
  return op->getLHS()->getType();
}

} // namespace

void ShiftRangeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("shift"), this);
}

void ShiftRangeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *op = result.Nodes.getNodeAs<BinaryOperator>("shift");
  if (!op || !result.Context || !result.SourceManager || !isShift(op->getOpcode())) {
    return;
  }
  const QualType shifted = shiftedType(op);
  if (shifted.isNull() || !shifted->isIntegerType()) {
    return;
  }
  const unsigned width = result.Context->getIntWidth(shifted);
  if (width == 0 || width > 128) {
    return;
  }

  // A non-constant amount is not decided here. That needs dataflow.
  clang::Expr::EvalResult value;
  const Expr *amount = op->getRHS();
  if (!amount || !amount->EvaluateAsInt(value, *result.Context) || !value.Val.isInt()) {
    return;
  }
  llvm::APSInt bits = value.Val.getInt();
  const llvm::APSInt limit(llvm::APInt(std::max(bits.getBitWidth(), 32u), width), true);
  bits = bits.extend(limit.getBitWidth());
  if (!bits.isNegative() && bits.ult(limit)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                amount->getBeginLoc(),
                "ss.conv.shift-range",
                "shift amount is outside the bit width");
}

#include "siliscope/NoSignedBitwise.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::Expr;
using clang::QualType;
using clang::UnaryOperator;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::unaryOperator;

namespace {

static bool isBitwiseOpcode(clang::BinaryOperator::Opcode op) {
  switch (op) {
  case clang::BO_And:
  case clang::BO_Or:
  case clang::BO_Xor:
  case clang::BO_Shl:
  case clang::BO_Shr:
  case clang::BO_AndAssign:
  case clang::BO_OrAssign:
  case clang::BO_XorAssign:
  case clang::BO_ShlAssign:
  case clang::BO_ShrAssign:
    return true;
  default:
    return false;
  }
}

// Type as written, before integer promotions. uint8_t is unsigned even though
// the promotion is signed int.
static bool isSignedIntegerOperand(const Expr *e) {
  if (!e) {
    return false;
  }
  const Expr *inner = e->IgnoreParenImpCasts();
  if (!inner) {
    return false;
  }
  QualType t = inner->getType();
  if (t->isEnumeralType()) {
    if (const auto *et = t->getAs<clang::EnumType>()) {
      t = et->getDecl()->getIntegerType();
    }
  }
  if (t.isNull() || !t->isIntegerType()) {
    return false;
  }
  return !t->isUnsignedIntegerType();
}

} // namespace

void NoSignedBitwiseCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("bin"), this);
  finder.addMatcher(unaryOperator(hasOperatorName("~")).bind("not"), this);
}

void NoSignedBitwiseCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager) {
    return;
  }
  const Expr *operands[2] = {nullptr, nullptr};
  unsigned n = 0;
  if (const auto *bin = result.Nodes.getNodeAs<BinaryOperator>("bin")) {
    if (!isBitwiseOpcode(bin->getOpcode())) {
      return;
    }
    operands[n++] = bin->getLHS();
    operands[n++] = bin->getRHS();
  } else if (const auto *un = result.Nodes.getNodeAs<UnaryOperator>("not")) {
    operands[n++] = un->getSubExpr();
  } else {
    return;
  }
  for (unsigned i = 0; i < n; ++i) {
    if (!isSignedIntegerOperand(operands[i])) {
      continue;
    }
    reporter.emit(*result.SourceManager,
                  operands[i]->getBeginLoc(),
                  "ss.conv.no-signed-bitwise",
                  "bitwise operand must be unsigned");
  }
}

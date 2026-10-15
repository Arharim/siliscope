#include "siliscope/DeclVolatile.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <map>
#include <string>

using clang::BinaryOperator;
using clang::CXXBoolLiteralExpr;
using clang::DeclRefExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::IntegerLiteral;
using clang::QualType;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.decl.volatile";
constexpr const char *kMsg = "volatile is not a lock";

bool allowed(const VarDecl *var, Reporter &reporter) {
  if (!var || !var->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = var->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

const VarDecl *volatileScalar(const Expr *expr) {
  if (!expr) {
    return nullptr;
  }
  const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr->IgnoreParenImpCasts());
  if (!ref) {
    return nullptr;
  }
  const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl());
  if (!var || !var->hasGlobalStorage()) {
    return nullptr;
  }
  const QualType type = var->getType();
  if (type.isNull() || !type.isVolatileQualified() || type->isPointerType() ||
      type->isReferenceType() || type->isRecordType()) {
    return nullptr;
  }
  return var->getCanonicalDecl();
}

bool isOne(const Expr *expr) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  if (const auto *lit = llvm::dyn_cast<IntegerLiteral>(expr)) {
    return lit->getValue().isOne();
  }
  if (const auto *boolean = llvm::dyn_cast<CXXBoolLiteralExpr>(expr)) {
    return boolean->getValue();
  }
  return false;
}

bool isZero(const Expr *expr) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  if (const auto *lit = llvm::dyn_cast<IntegerLiteral>(expr)) {
    return lit->getValue().isZero();
  }
  if (const auto *boolean = llvm::dyn_cast<CXXBoolLiteralExpr>(expr)) {
    return !boolean->getValue();
  }
  return false;
}

struct Claim {
  bool stored = false;
  bool tested = false;
  clang::SourceLocation at;
};

class FindClaim : public clang::RecursiveASTVisitor<FindClaim> {
public:
  std::map<const VarDecl *, Claim> claims;

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!op || op->getOpcode() != clang::UO_LNot) {
      return true;
    }
    if (const VarDecl *var = volatileScalar(op->getSubExpr())) {
      claims[var].tested = true;
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!op) {
      return true;
    }
    if (op->getOpcode() == clang::BO_Assign) {
      const VarDecl *var = volatileScalar(op->getLHS());
      if (var && isOne(op->getRHS()) && !op->isCompoundAssignmentOp()) {
        Claim &claim = claims[var];
        claim.stored = true;
        if (claim.at.isInvalid()) {
          claim.at = op->getExprLoc();
        }
      }
      return true;
    }
    if (op->getOpcode() != clang::BO_EQ && op->getOpcode() != clang::BO_NE) {
      return true;
    }
    const VarDecl *left = volatileScalar(op->getLHS());
    const VarDecl *right = volatileScalar(op->getRHS());
    if (left && isZero(op->getRHS())) {
      claims[left].tested = true;
    }
    if (right && isZero(op->getLHS())) {
      claims[right].tested = true;
    }
    return true;
  }
};

} // namespace

void DeclVolatileCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void DeclVolatileCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || fn->isTemplateInstantiation()) {
    return;
  }
  FindClaim walk;
  walk.TraverseStmt(fn->getBody());
  for (const auto &item : walk.claims) {
    if (!item.second.stored || !item.second.tested || allowed(item.first, reporter)) {
      continue;
    }
    reporter.emit(*result.SourceManager, item.second.at, kId, kMsg);
  }
}

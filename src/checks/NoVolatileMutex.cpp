#include "siliscope/NoVolatileMutex.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>
#include <vector>

using clang::BinaryOperator;
using clang::DeclRefExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::QualType;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.conc.no-volatile-mutex";
constexpr const char *kMsg = "volatile does not make this read-modify-write atomic";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

// A shared volatile scalar. A pointer or a field is a register, not this hint.
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

class Mentions : public clang::RecursiveASTVisitor<Mentions> {
public:
  const VarDecl *want = nullptr;
  bool found = false;

  bool VisitDeclRefExpr(DeclRefExpr *ref) {
    if (found || !ref || !want) {
      return true;
    }
    if (const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl())) {
      found = var->getCanonicalDecl() == want;
    }
    return !found;
  }
};

bool mentions(const Expr *expr, const VarDecl *var) {
  if (!expr || !var) {
    return false;
  }
  Mentions walk;
  walk.want = var;
  walk.TraverseStmt(const_cast<Expr *>(expr));
  return walk.found;
}

class FindRmw : public clang::RecursiveASTVisitor<FindRmw> {
public:
  std::vector<clang::SourceLocation> at;

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!op || !op->isIncrementDecrementOp()) {
      return true;
    }
    if (volatileScalar(op->getSubExpr())) {
      at.push_back(op->getExprLoc());
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!op || !op->isAssignmentOp()) {
      return true;
    }
    const VarDecl *var = volatileScalar(op->getLHS());
    if (!var) {
      return true;
    }
    if (op->isCompoundAssignmentOp() || mentions(op->getRHS(), var)) {
      at.push_back(op->getExprLoc());
    }
    return true;
  }
};

} // namespace

void NoVolatileMutexCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoVolatileMutexCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || fn->isTemplateInstantiation() ||
      allowed(fn, reporter)) {
    return;
  }
  FindRmw walk;
  walk.TraverseStmt(fn->getBody());
  for (clang::SourceLocation at : walk.at) {
    reporter.emit(*result.SourceManager, at, kId, kMsg);
  }
}

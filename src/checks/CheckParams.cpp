#include "siliscope/CheckParams.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::ArraySubscriptExpr;
using clang::BinaryOperator;
using clang::CallExpr;
using clang::DeclRefExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::MemberExpr;
using clang::ParmVarDecl;
using clang::QualType;
using clang::SourceLocation;
using clang::SourceManager;
using clang::UnaryOperator;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.fn.check-params";
constexpr const char *kNull = "reject a null pointer";
constexpr const char *kIndex = "check this index against its bound";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool isParm(const Expr *expr, const ParmVarDecl *parm) {
  if (!expr || !parm) {
    return false;
  }
  const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr->IgnoreParenImpCasts());
  if (!ref) {
    return false;
  }
  const auto *var = llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
  return var && var->getCanonicalDecl() == parm->getCanonicalDecl();
}

bool published(const FunctionDecl *fn, const SourceManager &sm) {
  if (!fn->isExternallyVisible() || fn->isMain() || isIsr(fn)) {
    return false;
  }
  if (fn->getDeclName().isIdentifier() && fn->getName() == "main") {
    return false;
  }
  if (fn->getAccess() == clang::AS_private || fn->getAccess() == clang::AS_protected) {
    return false;
  }
  for (const FunctionDecl *redecl : fn->redecls()) {
    const SourceLocation loc = sm.getSpellingLoc(redecl->getLocation());
    if (loc.isInvalid() || sm.isInSystemHeader(loc) || sm.isWrittenInMainFile(loc)) {
      continue;
    }
    return true;
  }
  return false;
}

bool isAssert(llvm::StringRef name) {
  return name == "assert" || name == "ASSERT" || name == "configASSERT" ||
         name.contains_insensitive("assert");
}

class Scan : public clang::RecursiveASTVisitor<Scan> {
public:
  const ParmVarDecl *parm = nullptr;
  clang::ASTContext *ctx = nullptr;
  bool nullTest = false;
  bool rangeTest = false;
  SourceLocation derefAt;
  SourceLocation indexAt;

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (!op || !parm) {
      return true;
    }
    if (op->getOpcode() == clang::UO_LNot && isParm(op->getSubExpr(), parm)) {
      nullTest = true;
    }
    if (op->getOpcode() == clang::UO_Deref && isParm(op->getSubExpr(), parm) &&
        derefAt.isInvalid()) {
      derefAt = op->getExprLoc();
    }
    return true;
  }

  bool VisitMemberExpr(MemberExpr *expr) {
    if (expr && expr->isArrow() && isParm(expr->getBase(), parm) && derefAt.isInvalid()) {
      derefAt = expr->getExprLoc();
    }
    return true;
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *expr) {
    if (!expr || !parm) {
      return true;
    }
    if (isParm(expr->getBase(), parm) && derefAt.isInvalid()) {
      derefAt = expr->getExprLoc();
    }
    if (mentionsIndex(expr->getIdx()) && indexAt.isInvalid()) {
      indexAt = expr->getIdx()->getExprLoc();
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (!op || !parm || !ctx) {
      return true;
    }
    const clang::BinaryOperatorKind kind = op->getOpcode();
    const bool equality = kind == clang::BO_EQ || kind == clang::BO_NE;
    const bool relational = kind == clang::BO_LT || kind == clang::BO_GT || kind == clang::BO_LE ||
                            kind == clang::BO_GE;
    if (equality && ((isParm(op->getLHS(), parm) && isNull(op->getRHS())) ||
                     (isParm(op->getRHS(), parm) && isNull(op->getLHS())))) {
      nullTest = true;
    }
    if (relational && (isParm(op->getLHS(), parm) || isParm(op->getRHS(), parm))) {
      rangeTest = true;
    }
    return true;
  }

  bool VisitCallExpr(CallExpr *call) {
    if (!call || !parm) {
      return true;
    }
    if (call->getCallee() && isParm(call->getCallee(), parm) && derefAt.isInvalid()) {
      derefAt = call->getExprLoc();
    }
    const FunctionDecl *callee = call->getDirectCallee();
    if (!callee || !callee->getDeclName().isIdentifier() || !isAssert(callee->getName())) {
      return true;
    }
    for (const Expr *arg : call->arguments()) {
      if (mentionsIndex(arg)) {
        nullTest = true;
        rangeTest = true;
      }
    }
    return true;
  }

private:
  bool isNull(const Expr *expr) const {
    return expr && ctx &&
           expr->isNullPointerConstant(*ctx, Expr::NPC_ValueDependentIsNotNull) !=
               Expr::NPCK_NotNull;
  }

  bool mentionsIndex(const Expr *expr) const {
    if (!expr || !parm) {
      return false;
    }
    class Walk : public clang::RecursiveASTVisitor<Walk> {
    public:
      const ParmVarDecl *parm = nullptr;
      bool found = false;
      bool VisitDeclRefExpr(DeclRefExpr *ref) {
        if (!found && isParm(ref, parm)) {
          found = true;
        }
        return !found;
      }
    } walk;
    walk.parm = parm;
    walk.TraverseStmt(const_cast<Expr *>(expr));
    return walk.found;
  }
};

bool pointerParam(const ParmVarDecl *parm) {
  const QualType type = parm->getType();
  return !type.isNull() && type->isPointerType();
}

bool indexParam(const ParmVarDecl *parm) {
  const QualType type = parm->getType().getNonReferenceType();
  if (type.isNull() || type->isBooleanType() || type->isAnyPointerType()) {
    return false;
  }
  return type->isIntegerType();
}

} // namespace

void CheckParamsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void CheckParamsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || !result.Context ||
      fn->isTemplateInstantiation() || !published(fn, *result.SourceManager) ||
      allowed(fn, reporter)) {
    return;
  }
  for (const ParmVarDecl *parm : fn->parameters()) {
    if (!parm) {
      continue;
    }
    Scan scan;
    scan.parm = parm;
    scan.ctx = result.Context;
    scan.TraverseStmt(fn->getBody());
    if (pointerParam(parm) && scan.derefAt.isValid() && !scan.nullTest) {
      reporter.emit(*result.SourceManager, scan.derefAt, kId, kNull);
    }
    if (indexParam(parm) && scan.indexAt.isValid() && !scan.rangeTest) {
      reporter.emit(*result.SourceManager, scan.indexAt, kId, kIndex);
    }
  }
}

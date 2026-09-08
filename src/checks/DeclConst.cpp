#include "siliscope/DeclConst.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <unordered_set>

using clang::BinaryOperator;
using clang::CallExpr;
using clang::CompoundAssignOperator;
using clang::DeclRefExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::MemberExpr;
using clang::ParmVarDecl;
using clang::QualType;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

static const VarDecl *asVar(const clang::ValueDecl *d) {
  const auto *vd = llvm::dyn_cast_or_null<VarDecl>(d);
  return vd ? vd->getCanonicalDecl() : nullptr;
}

static const Expr *strip(const Expr *e) {
  return e ? e->IgnoreParenImpCasts() : nullptr;
}

static bool isPtrToVoid(QualType t) {
  if (t.isNull() || !t->isPointerType()) {
    return false;
  }
  return t->getPointeeType()->isVoidType();
}

class MutVisitor : public clang::RecursiveASTVisitor<MutVisitor> {
public:
  std::unordered_set<const VarDecl *> object_mut;
  std::unordered_set<const VarDecl *> pointee_mut;

  bool TraverseFunctionDecl(FunctionDecl *d) {
    if (outer && d != outer) {
      return true;
    }
    return RecursiveASTVisitor::TraverseFunctionDecl(d);
  }

  bool VisitBinaryOperator(BinaryOperator *op) {
    if (op && op->isAssignmentOp()) {
      markStore(op->getLHS());
    }
    return true;
  }

  bool VisitCompoundAssignOperator(CompoundAssignOperator *op) {
    if (op) {
      markStore(op->getLHS());
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *op) {
    if (op && op->isIncrementDecrementOp()) {
      markStore(op->getSubExpr());
    }
    return true;
  }

  bool VisitCallExpr(CallExpr *c) {
    if (!c) {
      return true;
    }
    const FunctionDecl *callee = c->getDirectCallee();
    const unsigned n = c->getNumArgs();
    if (!callee) {
      for (unsigned i = 0; i < n; ++i) {
        assumeWritable(c->getArg(i));
      }
      return true;
    }
    const unsigned np = callee->getNumParams();
    for (unsigned i = 0; i < n; ++i) {
      if (i < np) {
        markCallArg(callee->getParamDecl(i)->getType(), c->getArg(i));
      } else {
        assumeWritable(c->getArg(i));
      }
    }
    return true;
  }

  void setOuter(const FunctionDecl *fn) { outer = fn; }

private:
  const FunctionDecl *outer = nullptr;

  void mutObject(const VarDecl *vd) {
    if (vd) {
      object_mut.insert(vd);
    }
  }

  void mutPointee(const VarDecl *vd) {
    if (vd) {
      pointee_mut.insert(vd);
    }
  }

  const VarDecl *declRefVar(const Expr *e) {
    e = strip(e);
    if (const auto *dre = llvm::dyn_cast_or_null<DeclRefExpr>(e)) {
      return asVar(dre->getDecl());
    }
    return nullptr;
  }

  void markPointee(const Expr *e) {
    const VarDecl *vd = declRefVar(e);
    if (!vd) {
      return;
    }
    if (vd->getType()->isArrayType()) {
      mutObject(vd);
    } else if (vd->getType()->isAnyPointerType()) {
      mutPointee(vd);
    }
  }

  void markStore(const Expr *e) {
    e = strip(e);
    if (!e) {
      return;
    }
    if (const VarDecl *vd = declRefVar(e)) {
      mutObject(vd);
      return;
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_Deref) {
        markPointee(uo->getSubExpr());
      }
      return;
    }
    if (const auto *as = llvm::dyn_cast<clang::ArraySubscriptExpr>(e)) {
      markPointee(as->getBase());
      return;
    }
    if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
      if (me->isArrow()) {
        markPointee(me->getBase());
      } else {
        markStore(me->getBase());
      }
    }
  }

  void assumeWritable(const Expr *arg) {
    arg = strip(arg);
    if (!arg) {
      return;
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(arg)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        mutObject(declRefVar(uo->getSubExpr()));
        return;
      }
    }
    markPointee(arg);
  }

  void markCallArg(QualType pt, const Expr *arg) {
    if (pt.isNull() || !arg) {
      return;
    }
    if (pt->isReferenceType()) {
      if (!pt->getPointeeType().isConstQualified()) {
        mutObject(declRefVar(arg));
      }
      return;
    }
    if (!pt->isPointerType() && !pt->isAnyPointerType()) {
      return;
    }
    if (pt->getPointeeType().isConstQualified()) {
      return;
    }
    arg = strip(arg);
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(arg)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        mutObject(declRefVar(uo->getSubExpr()));
        return;
      }
    }
    markPointee(arg);
  }
};

static bool skipVar(const VarDecl *d) {
  if (!d || d->isImplicit() || d->getType().isNull()) {
    return true;
  }
  QualType t = d->getType();
  if (t.isVolatileQualified() || t.getNonReferenceType().isVolatileQualified()) {
    return true;
  }
  return false;
}

} // namespace

void DeclConstCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void DeclConstCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager) {
    return;
  }
  MutVisitor v;
  v.setOuter(fn);
  v.TraverseStmt(fn->getBody());
  for (const auto *p : fn->parameters()) {
    v.TraverseDecl(const_cast<ParmVarDecl *>(p));
  }

  for (const auto *p : fn->parameters()) {
    const VarDecl *vd = p->getCanonicalDecl();
    if (skipVar(vd)) {
      continue;
    }
    QualType t = vd->getType();
    if (t->isReferenceType()) {
      if (t->getPointeeType().isConstQualified() || v.object_mut.count(vd)) {
        continue;
      }
      reporter.emit(*result.SourceManager,
                    vd->getLocation(),
                    "ss.decl.const",
                    "reference parameter is never written; it can be const");
      continue;
    }
    if (!t->isAnyPointerType() || isPtrToVoid(t)) {
      continue;
    }
    const QualType pointee = t->getPointeeType();
    if (pointee->isFunctionType() || pointee.isConstQualified() || pointee.isVolatileQualified() ||
        v.pointee_mut.count(vd)) {
      continue;
    }
    reporter.emit(*result.SourceManager,
                  vd->getLocation(),
                  "ss.decl.const",
                  "pointer parameter is never written; make the pointee const");
  }

  class Locals : public clang::RecursiveASTVisitor<Locals> {
  public:
    const FunctionDecl *outer = nullptr;
    std::vector<const VarDecl *> vars;
    bool TraverseFunctionDecl(FunctionDecl *d) {
      if (outer && d != outer) {
        return true;
      }
      return RecursiveASTVisitor::TraverseFunctionDecl(d);
    }
    bool VisitVarDecl(VarDecl *d) {
      if (d && !llvm::isa<ParmVarDecl>(d) && (d->hasLocalStorage() || d->isStaticLocal())) {
        vars.push_back(d->getCanonicalDecl());
      }
      return true;
    }
  } locals;
  locals.outer = fn;
  locals.TraverseStmt(fn->getBody());

  for (const VarDecl *vd : locals.vars) {
    if (skipVar(vd) || vd->getType().isConstQualified() || v.object_mut.count(vd)) {
      continue;
    }
    reporter.emit(*result.SourceManager,
                  vd->getLocation(),
                  "ss.decl.const",
                  "local is never reassigned; it can be const");
  }
}

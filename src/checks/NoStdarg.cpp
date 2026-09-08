#include "siliscope/NoStdarg.h"

#include "siliscope/Report.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::FunctionDecl;
using clang::QualType;
using clang::VarDecl;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::isVariadic;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

static bool isVaListType(QualType t, const clang::ASTContext &ctx) {
  if (t.isNull()) {
    return false;
  }
  t = t.getNonReferenceType();
  const QualType va = ctx.getBuiltinVaListType();
  if (va.isNull()) {
    return false;
  }
  if (t.getCanonicalType().getUnqualifiedType() == va.getCanonicalType().getUnqualifiedType()) {
    return true;
  }
  if (const auto *p = t->getAs<clang::PointerType>()) {
    return isVaListType(p->getPointeeType(), ctx);
  }
  return false;
}

static bool stdargAllowed(const Reporter &r, const FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return r.allows("ss.fn.no-stdarg", name.c_str());
}

static const FunctionDecl *enclosingFunction(const clang::Decl *d) {
  const clang::DeclContext *dc = d->getDeclContext();
  while (dc) {
    if (const auto *fn = llvm::dyn_cast<FunctionDecl>(dc)) {
      return fn;
    }
    dc = dc->getParent();
  }
  return nullptr;
}

static const FunctionDecl *enclosingFunction(const clang::Stmt *s, clang::ASTContext &ctx) {
  const clang::Stmt *cur = s;
  for (int i = 0; i < 64; ++i) {
    const auto parents = ctx.getParents(*cur);
    if (parents.empty()) {
      return nullptr;
    }
    if (const auto *fn = parents[0].get<FunctionDecl>()) {
      return fn;
    }
    const auto *next = parents[0].get<clang::Stmt>();
    if (!next) {
      return nullptr;
    }
    cur = next;
  }
  return nullptr;
}

void NoStdargCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isVariadic(), unless(isImplicit())).bind("fn"), this);
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("__builtin_va_start",
                                                            "__builtin_va_end",
                                                            "__builtin_va_copy",
                                                            "va_start",
                                                            "va_end",
                                                            "va_copy"))))
                        .bind("call"),
                    this);
  finder.addMatcher(varDecl(unless(isImplicit())).bind("var"), this);
}

void NoStdargCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  clang::SourceLocation at;
  if (const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn")) {
    if (fn->getBuiltinID() != 0 || !fn->isFirstDecl()) {
      return;
    }
    if (stdargAllowed(reporter, fn)) {
      return;
    }
    at = fn->getLocation();
  } else if (const auto *c = result.Nodes.getNodeAs<CallExpr>("call")) {
    if (result.Context && stdargAllowed(reporter, enclosingFunction(c, *result.Context))) {
      return;
    }
    at = c->getBeginLoc();
  } else if (const auto *vd = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (!result.Context || !isVaListType(vd->getType(), *result.Context)) {
      return;
    }
    if (stdargAllowed(reporter, enclosingFunction(vd))) {
      return;
    }
    at = vd->getLocation();
  } else {
    return;
  }
  reporter.emit(
      *result.SourceManager, at, "ss.fn.no-stdarg", "do not use va_list or variadic functions");
}

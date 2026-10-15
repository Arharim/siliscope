#include "siliscope/NoDeadCode.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::ASTContext;
using clang::AttributedStmt;
using clang::CastExpr;
using clang::CompoundStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FullExpr;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::LabelStmt;
using clang::SourceManager;
using clang::Stmt;
using clang::SwitchStmt;
using clang::WhileStmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.ctrl.no-dead-code";
constexpr const char *kMsg = "remove this effect-free statement";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

// (void)expr is the project's discard. A volatile read and a call still have an effect.
bool voidDiscard(const Expr *expr) {
  expr = expr->IgnoreParens();
  const auto *cast = llvm::dyn_cast<CastExpr>(expr);
  return cast && cast->getCastKind() == clang::CK_ToVoid;
}

const Expr *peel(const Expr *expr) {
  while (expr) {
    if (const auto *full = llvm::dyn_cast<FullExpr>(expr)) {
      expr = full->getSubExpr();
      continue;
    }
    if (const auto *paren = llvm::dyn_cast<clang::ParenExpr>(expr)) {
      expr = paren->getSubExpr();
      continue;
    }
    break;
  }
  return expr;
}

void consider(const Expr *expr, ASTContext &ctx, const SourceManager &sm, Reporter &reporter) {
  if (!expr || expr->HasSideEffects(ctx)) {
    return;
  }
  const Expr *inner = peel(expr);
  if (!inner || inner->HasSideEffects(ctx) || voidDiscard(inner)) {
    return;
  }
  reporter.emit(sm, expr->getBeginLoc(), kId, kMsg);
}

void walk(const Stmt *stmt, ASTContext &ctx, const SourceManager &sm, Reporter &reporter) {
  if (!stmt) {
    return;
  }
  if (const auto *compound = llvm::dyn_cast<CompoundStmt>(stmt)) {
    for (const Stmt *child : compound->body()) {
      walk(child, ctx, sm, reporter);
    }
    return;
  }
  if (const auto *label = llvm::dyn_cast<LabelStmt>(stmt)) {
    walk(label->getSubStmt(), ctx, sm, reporter);
    return;
  }
  if (const auto *cas = llvm::dyn_cast<clang::CaseStmt>(stmt)) {
    walk(cas->getSubStmt(), ctx, sm, reporter);
    return;
  }
  if (const auto *def = llvm::dyn_cast<clang::DefaultStmt>(stmt)) {
    walk(def->getSubStmt(), ctx, sm, reporter);
    return;
  }
  if (const auto *iff = llvm::dyn_cast<IfStmt>(stmt)) {
    walk(iff->getInit(), ctx, sm, reporter);
    walk(iff->getThen(), ctx, sm, reporter);
    walk(iff->getElse(), ctx, sm, reporter);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<WhileStmt>(stmt)) {
    walk(loop->getBody(), ctx, sm, reporter);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<DoStmt>(stmt)) {
    walk(loop->getBody(), ctx, sm, reporter);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<ForStmt>(stmt)) {
    if (const Stmt *init = loop->getInit()) {
      if (const auto *initExpr = llvm::dyn_cast<Expr>(init)) {
        consider(initExpr, ctx, sm, reporter);
      } else {
        walk(init, ctx, sm, reporter);
      }
    }
    consider(loop->getInc(), ctx, sm, reporter);
    walk(loop->getBody(), ctx, sm, reporter);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<clang::CXXForRangeStmt>(stmt)) {
    walk(loop->getBody(), ctx, sm, reporter);
    return;
  }
  if (const auto *sw = llvm::dyn_cast<SwitchStmt>(stmt)) {
    walk(sw->getInit(), ctx, sm, reporter);
    walk(sw->getBody(), ctx, sm, reporter);
    return;
  }
  if (const auto *tried = llvm::dyn_cast<clang::CXXTryStmt>(stmt)) {
    walk(tried->getTryBlock(), ctx, sm, reporter);
    for (unsigned i = 0; i < tried->getNumHandlers(); ++i) {
      walk(tried->getHandler(i)->getHandlerBlock(), ctx, sm, reporter);
    }
    return;
  }
  if (const auto *attr = llvm::dyn_cast<AttributedStmt>(stmt)) {
    walk(attr->getSubStmt(), ctx, sm, reporter);
    return;
  }
  if (const auto *expr = llvm::dyn_cast<Expr>(stmt)) {
    consider(expr, ctx, sm, reporter);
  }
}

} // namespace

void NoDeadCodeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoDeadCodeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.Context || !result.SourceManager || allowed(fn, reporter)) {
    return;
  }
  walk(fn->getBody(), *result.Context, *result.SourceManager, reporter);
}

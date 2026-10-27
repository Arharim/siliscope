#include "siliscope/NoAssignAsValue.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::ASTContext;
using clang::BinaryOperator;
using clang::CXXOperatorCallExpr;
using clang::DeclRefExpr;
using clang::DoStmt;
using clang::DynTypedNode;
using clang::Expr;
using clang::ForStmt;
using clang::IfStmt;
using clang::MemberExpr;
using clang::Stmt;
using clang::SwitchStmt;
using clang::VarDecl;
using clang::WhileStmt;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::cxxOperatorCallExpr;
using clang::ast_matchers::hasOverloadedOperatorName;
using clang::ast_matchers::isAssignmentOperator;

namespace {

constexpr const char *kId = "ss.conv.no-assign-as-value";
constexpr const char *kMsg = "do not use the value of an assignment";

const Expr *strip(const Expr *expr) {
  return expr ? expr->IgnoreParenImpCasts() : nullptr;
}

bool wrapper(const Stmt *stmt) {
  return llvm::isa<clang::ParenExpr>(stmt) || llvm::isa<clang::ImplicitCastExpr>(stmt) ||
         llvm::isa<clang::ExprWithCleanups>(stmt) ||
         llvm::isa<clang::MaterializeTemporaryExpr>(stmt) ||
         llvm::isa<clang::CXXBindTemporaryExpr>(stmt);
}

bool same(const Stmt *left, const Stmt *right) {
  if (!left || !right) {
    return false;
  }
  if (left == right) {
    return true;
  }
  const auto *lhs = llvm::dyn_cast<Expr>(left);
  const auto *rhs = llvm::dyn_cast<Expr>(right);
  return lhs && rhs && strip(lhs) == strip(rhs);
}

// The comma operator throws the left value away. Everything else that reads
// this expression uses it.
bool valueUsed(const Expr *expr, ASTContext &ctx) {
  const Stmt *cur = expr;
  DynTypedNode node = DynTypedNode::create(*expr);
  for (int step = 0; step < 32; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    const Stmt *parent = node.get<Stmt>();
    if (!parent) {
      return node.get<VarDecl>() != nullptr;
    }
    if (wrapper(parent)) {
      cur = parent;
      continue;
    }
    if (const auto *comma = llvm::dyn_cast<BinaryOperator>(parent)) {
      if (comma->getOpcode() == clang::BO_Comma) {
        if (same(comma->getLHS(), cur)) {
          return false;
        }
        cur = comma;
        continue;
      }
    }
    if (llvm::isa<Expr>(parent)) {
      return true;
    }
    if (const auto *iff = llvm::dyn_cast<IfStmt>(parent)) {
      return same(iff->getCond(), cur);
    }
    if (const auto *loop = llvm::dyn_cast<ForStmt>(parent)) {
      return same(loop->getCond(), cur);
    }
    if (const auto *loop = llvm::dyn_cast<WhileStmt>(parent)) {
      return same(loop->getCond(), cur);
    }
    if (const auto *loop = llvm::dyn_cast<DoStmt>(parent)) {
      return same(loop->getCond(), cur);
    }
    if (const auto *sw = llvm::dyn_cast<SwitchStmt>(parent)) {
      return same(sw->getCond(), cur);
    }
    if (llvm::isa<clang::ReturnStmt>(parent)) {
      return true;
    }
    return false;
  }
  return true;
}

bool simpleTarget(const Expr *expr) {
  expr = strip(expr);
  if (!expr) {
    return false;
  }
  if (const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr)) {
    return llvm::isa<VarDecl>(ref->getDecl());
  }
  const auto *member = llvm::dyn_cast<MemberExpr>(expr);
  if (!member) {
    return false;
  }
  const Expr *base = strip(member->getBase());
  return base && (llvm::isa<DeclRefExpr>(base) || llvm::isa<clang::CXXThisExpr>(base));
}

bool literal(const Expr *expr) {
  expr = strip(expr);
  if (!expr) {
    return false;
  }
  if (const auto *unary = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
    const auto op = unary->getOpcode();
    if (op == clang::UO_Plus || op == clang::UO_Minus || op == clang::UO_Not ||
        op == clang::UO_LNot) {
      return literal(unary->getSubExpr());
    }
    return false;
  }
  return llvm::isa<clang::IntegerLiteral>(expr) || llvm::isa<clang::FloatingLiteral>(expr) ||
         llvm::isa<clang::CharacterLiteral>(expr) || llvm::isa<clang::CXXBoolLiteralExpr>(expr) ||
         llvm::isa<clang::CXXNullPtrLiteralExpr>(expr);
}

// `a = b = 0;` as a statement. The constant is copied through simple targets
// and is not read by a condition, a call, or an initializer.
bool initChain(const BinaryOperator *top) {
  if (!top || top->getOpcode() != clang::BO_Assign) {
    return false;
  }
  bool nested = false;
  const Expr *cur = top;
  while (const auto *bin = llvm::dyn_cast<BinaryOperator>(strip(cur))) {
    if (bin->getOpcode() != clang::BO_Assign) {
      return false;
    }
    if (!simpleTarget(bin->getLHS())) {
      return false;
    }
    const Expr *rhs = strip(bin->getRHS());
    if (const auto *next = llvm::dyn_cast<BinaryOperator>(rhs)) {
      if (next->getOpcode() == clang::BO_Assign) {
        nested = true;
        cur = next;
        continue;
      }
    }
    return nested && literal(rhs);
  }
  return false;
}

bool savedByInit(const Expr *assign, ASTContext &ctx) {
  const Expr *top = assign;
  for (int step = 0; step < 16; ++step) {
    const auto parents = ctx.getParents(*top);
    if (parents.empty()) {
      break;
    }
    const Stmt *parent = parents[0].get<Stmt>();
    if (!parent) {
      break;
    }
    if (wrapper(parent)) {
      top = llvm::cast<Expr>(parent);
      continue;
    }
    const auto *bin = llvm::dyn_cast<BinaryOperator>(parent);
    if (!bin || bin->getOpcode() != clang::BO_Assign || !same(bin->getRHS(), top)) {
      break;
    }
    top = bin;
  }
  const auto *bin = llvm::dyn_cast<BinaryOperator>(strip(top));
  if (!bin || valueUsed(bin, ctx)) {
    return false;
  }
  return initChain(bin);
}

std::string targetName(const Expr *lhs) {
  lhs = strip(lhs);
  if (!lhs) {
    return {};
  }
  if (const auto *ref = llvm::dyn_cast<DeclRefExpr>(lhs)) {
    if (const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl())) {
      return var->getName().str();
    }
  }
  if (const auto *member = llvm::dyn_cast<MemberExpr>(lhs)) {
    return member->getMemberDecl()->getName().str();
  }
  return {};
}

} // namespace

void NoAssignAsValueCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator(isAssignmentOperator()).bind("bin"), this);
  finder.addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("=")).bind("call"), this);
}

void NoAssignAsValueCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  const Expr *expr = result.Nodes.getNodeAs<BinaryOperator>("bin");
  const Expr *lhs = nullptr;
  if (const auto *bin = llvm::dyn_cast_or_null<BinaryOperator>(expr)) {
    lhs = bin->getLHS();
  } else if (const auto *call = result.Nodes.getNodeAs<CXXOperatorCallExpr>("call")) {
    expr = call;
    if (call->getNumArgs() > 0) {
      lhs = call->getArg(0);
    }
  }
  if (!expr || !valueUsed(expr, *result.Context) || savedByInit(expr, *result.Context)) {
    return;
  }
  const std::string name = targetName(lhs);
  if (!name.empty() && reporter.allows(kId, name.c_str())) {
    return;
  }
  const auto *bin = llvm::dyn_cast<BinaryOperator>(expr);
  const clang::SourceLocation loc = bin ? bin->getOperatorLoc() : expr->getBeginLoc();
  reporter.emit(*result.SourceManager, loc, kId, kMsg);
}

#include "siliscope/ForWellFormed.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"

#include <set>
#include <string>

using clang::ASTContext;
using clang::BinaryOperator;
using clang::CXXOperatorCallExpr;
using clang::DeclRefExpr;
using clang::DynTypedNode;
using clang::Expr;
using clang::FieldDecl;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::LambdaExpr;
using clang::MemberExpr;
using clang::OverloadedOperatorKind;
using clang::Stmt;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::forStmt;

namespace {

constexpr const char *kId = "ss.ctrl.for-well-formed";
constexpr const char *kCounter = "give this for one counter, changed only in the for-clause";
constexpr const char *kAssign = "do not assign in this for condition";

// A counter is one variable, or one field of one variable (including this->field).
struct Slot {
  const VarDecl *var = nullptr;
  const FieldDecl *field = nullptr;

  bool operator<(const Slot &other) const {
    if (var != other.var) {
      return var < other.var;
    }
    return field < other.field;
  }
};

const Expr *strip(const Expr *expr) {
  return expr ? expr->IgnoreParenImpCasts() : nullptr;
}

bool asSlot(const Expr *expr, Slot &out) {
  expr = strip(expr);
  if (!expr) {
    return false;
  }
  if (const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr)) {
    const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl());
    if (!var) {
      return false;
    }
    out = Slot{var->getCanonicalDecl(), nullptr};
    return true;
  }
  const auto *member = llvm::dyn_cast<MemberExpr>(expr);
  if (!member) {
    return false;
  }
  const auto *field = llvm::dyn_cast<FieldDecl>(member->getMemberDecl());
  if (!field) {
    return false;
  }
  const VarDecl *var = nullptr;
  if (const auto *ref = llvm::dyn_cast_or_null<DeclRefExpr>(strip(member->getBase()))) {
    if (const auto *base = llvm::dyn_cast<VarDecl>(ref->getDecl())) {
      var = base->getCanonicalDecl();
    }
  }
  out = Slot{var, field->getCanonicalDecl()};
  return true;
}

bool mutates(OverloadedOperatorKind op) {
  switch (op) {
  case clang::OO_PlusPlus:
  case clang::OO_MinusMinus:
  case clang::OO_Equal:
  case clang::OO_PlusEqual:
  case clang::OO_MinusEqual:
  case clang::OO_StarEqual:
  case clang::OO_SlashEqual:
  case clang::OO_PercentEqual:
  case clang::OO_AmpEqual:
  case clang::OO_PipeEqual:
  case clang::OO_CaretEqual:
  case clang::OO_LessLessEqual:
  case clang::OO_GreaterGreaterEqual:
    return true;
  default:
    return false;
  }
}

void written(const Stmt *stmt, std::set<Slot> &out) {
  if (!stmt || llvm::isa<LambdaExpr>(stmt)) {
    return;
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(stmt)) {
    Slot slot;
    if (unary->isIncrementDecrementOp() && asSlot(unary->getSubExpr(), slot)) {
      out.insert(slot);
    }
  } else if (const auto *binary = llvm::dyn_cast<BinaryOperator>(stmt)) {
    Slot slot;
    if (binary->isAssignmentOp() && asSlot(binary->getLHS(), slot)) {
      out.insert(slot);
    }
  } else if (const auto *call = llvm::dyn_cast<CXXOperatorCallExpr>(stmt)) {
    Slot slot;
    if (mutates(call->getOperator()) && call->getNumArgs() > 0 && asSlot(call->getArg(0), slot)) {
      out.insert(slot);
    }
  }
  for (const Stmt *child : stmt->children()) {
    written(child, out);
  }
}

void read(const Stmt *stmt, std::set<Slot> &out) {
  if (!stmt || llvm::isa<LambdaExpr>(stmt)) {
    return;
  }
  Slot slot;
  if (asSlot(llvm::dyn_cast<Expr>(stmt), slot)) {
    out.insert(slot);
  }
  for (const Stmt *child : stmt->children()) {
    read(child, out);
  }
}

// An assignment or ++/-- in the condition is not the for-clause update.
const Expr *extraAssign(const Stmt *stmt) {
  if (!stmt || llvm::isa<LambdaExpr>(stmt)) {
    return nullptr;
  }
  if (const auto *unary = llvm::dyn_cast<UnaryOperator>(stmt)) {
    if (unary->isIncrementDecrementOp()) {
      return unary;
    }
  } else if (const auto *binary = llvm::dyn_cast<BinaryOperator>(stmt)) {
    if (binary->isAssignmentOp()) {
      return binary;
    }
  } else if (const auto *call = llvm::dyn_cast<CXXOperatorCallExpr>(stmt)) {
    if (mutates(call->getOperator())) {
      return call;
    }
  }
  for (const Stmt *child : stmt->children()) {
    if (const Expr *found = extraAssign(child)) {
      return found;
    }
  }
  return nullptr;
}

bool allowed(const Stmt *stmt, ASTContext &ctx, Reporter &reporter) {
  DynTypedNode node = DynTypedNode::create(*stmt);
  for (int step = 0; step < 32; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    const auto *fn = node.get<FunctionDecl>();
    if (!fn) {
      continue;
    }
    if (fn->getDeclName().isEmpty()) {
      return false;
    }
    const std::string name = fn->getDeclName().getAsString();
    return !name.empty() && reporter.allows(kId, name.c_str());
  }
  return false;
}

} // namespace

void ForWellFormedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(forStmt().bind("for"), this);
}

void ForWellFormedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *loop = result.Nodes.getNodeAs<ForStmt>("for");
  // for (;;) has no counter. An infinite loop belongs to loop-bound.
  if (!loop || !loop->getCond() || !result.SourceManager || !result.Context) {
    return;
  }
  if (allowed(loop, *result.Context, reporter)) {
    return;
  }

  std::set<Slot> inInc;
  std::set<Slot> inCond;
  std::set<Slot> inBody;
  std::set<Slot> writtenCond;
  written(loop->getInc(), inInc);
  read(loop->getCond(), inCond);
  written(loop->getCond(), writtenCond);
  written(loop->getBody(), inBody);

  std::set<Slot> counters;
  for (const Slot &slot : inInc) {
    if (inCond.count(slot) != 0) {
      counters.insert(slot);
    }
  }

  const bool writtenOutside = counters.size() == 1 && (inBody.count(*counters.begin()) != 0 ||
                                                       writtenCond.count(*counters.begin()) != 0);
  if (counters.size() != 1 || writtenOutside) {
    reporter.emit(*result.SourceManager, loop->getForLoc(), kId, kCounter);
    return;
  }
  if (const Expr *assign = extraAssign(loop->getCond())) {
    reporter.emit(*result.SourceManager, assign->getExprLoc(), kId, kAssign);
  }
}

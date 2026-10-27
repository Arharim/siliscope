#include "siliscope/FloatLiteralSuffix.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::ASTContext;
using clang::BinaryOperator;
using clang::CallExpr;
using clang::DynTypedNode;
using clang::ExplicitCastExpr;
using clang::Expr;
using clang::FieldDecl;
using clang::FloatingLiteral;
using clang::FunctionDecl;
using clang::ImplicitCastExpr;
using clang::InitListExpr;
using clang::QualType;
using clang::ReturnStmt;
using clang::SourceManager;
using clang::Stmt;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::floatLiteral;

namespace {

constexpr const char *kId = "ss.expr.float-literal-suffix";
constexpr const char *kMsg = "suffix this float literal with f";

bool isFloat(QualType type) {
  if (type.isNull() || type->isDependentType()) {
    return false;
  }
  type = type.getNonReferenceType();
  if (const auto *array = type->getAsArrayTypeUnsafe()) {
    type = array->getElementType();
  }
  return type.getCanonicalType()->isSpecificBuiltinType(clang::BuiltinType::Float);
}

bool functionAllowed(const Stmt *stmt, ASTContext &ctx, Reporter &reporter) {
  DynTypedNode node = DynTypedNode::create(*stmt);
  for (int step = 0; step < 64; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      if (fn->getDeclName().isEmpty()) {
        return false;
      }
      const std::string name = fn->getDeclName().getAsString();
      return !name.empty() && reporter.allows(kId, name.c_str());
    }
  }
  return false;
}

// The literal is written as a double (or long double) and then stored,
// passed, or returned as float. A double destination stays quiet.
bool wantsFloat(const FloatingLiteral *lit, ASTContext &ctx) {
  DynTypedNode node = DynTypedNode::create(*lit);
  for (int step = 0; step < 32; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *cast = node.get<ImplicitCastExpr>()) {
      if (cast->getCastKind() == clang::CK_FloatingCast) {
        return isFloat(cast->getType());
      }
      continue;
    }
    if (const auto *cast = node.get<ExplicitCastExpr>()) {
      return isFloat(cast->getType());
    }
    if (const auto *bin = node.get<BinaryOperator>()) {
      if (bin->isAssignmentOp() || bin->isCompoundAssignmentOp()) {
        return isFloat(bin->getLHS()->getType());
      }
      continue;
    }
    if (const auto *var = node.get<VarDecl>()) {
      return isFloat(var->getType());
    }
    if (const auto *field = node.get<FieldDecl>()) {
      return isFloat(field->getType());
    }
    if (const auto *init = node.get<InitListExpr>()) {
      if (isFloat(init->getType())) {
        return true;
      }
      continue;
    }
    if (const auto *call = node.get<CallExpr>()) {
      const FunctionDecl *fn = call->getDirectCallee();
      if (!fn) {
        return false;
      }
      for (unsigned i = 0; i < call->getNumArgs() && i < fn->getNumParams(); ++i) {
        const Expr *arg = call->getArg(i)->IgnoreParenImpCasts();
        if (arg == lit || call->getArg(i)->IgnoreParenCasts() == lit) {
          return isFloat(fn->getParamDecl(i)->getType());
        }
      }
      return false;
    }
    if (const auto *ret = node.get<ReturnStmt>()) {
      DynTypedNode at = DynTypedNode::create(*ret);
      for (int up = 0; up < 64; ++up) {
        const auto above = ctx.getParents(at);
        if (above.empty()) {
          return false;
        }
        at = above[0];
        if (const auto *fn = at.get<FunctionDecl>()) {
          return isFloat(fn->getReturnType());
        }
      }
      return false;
    }
    if (node.get<clang::ParenExpr>() || node.get<UnaryOperator>() ||
        node.get<clang::ConditionalOperator>()) {
      continue;
    }
    if (node.get<Expr>()) {
      continue;
    }
    return false;
  }
  return false;
}

} // namespace

void FloatLiteralSuffixCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(floatLiteral().bind("lit"), this);
}

void FloatLiteralSuffixCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *lit = result.Nodes.getNodeAs<FloatingLiteral>("lit");
  if (!lit || !result.Context || !result.SourceManager) {
    return;
  }
  if (lit->getType()->isSpecificBuiltinType(clang::BuiltinType::Float)) {
    return;
  }
  if (!wantsFloat(lit, *result.Context) || functionAllowed(lit, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, lit->getBeginLoc(), kId, kMsg);
}

#include "siliscope/MemcmpString.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::CallExpr;
using clang::ExplicitCastExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::QualType;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;

namespace {

constexpr const char *kId = "ss.libc.memcmp-not-for-c-strings";
constexpr const char *kMsg = "do not memcmp a C string";

// Plain char and signed char are the C string types. unsigned char is a
// packed byte record, and so is any type that is not a character.
bool isStringChar(QualType type) {
  if (type.isNull()) {
    return false;
  }
  const auto *builtin = type->getAs<clang::BuiltinType>();
  if (!builtin) {
    return false;
  }
  switch (builtin->getKind()) {
  case clang::BuiltinType::Char_U:
  case clang::BuiltinType::Char_S:
  case clang::BuiltinType::SChar:
    return true;
  default:
    return false;
  }
}

// memcmp's parameters are void *. A cast to void * does not change which
// buffer was passed. A cast to char * is the type the caller wrote.
QualType bufferElement(const Expr *arg, clang::ASTContext &ctx) {
  const Expr *expr = arg->IgnoreParenImpCasts();
  while (const auto *cast = llvm::dyn_cast<ExplicitCastExpr>(expr)) {
    if (!cast->getType()->isVoidPointerType()) {
      break;
    }
    expr = cast->getSubExpr()->IgnoreParenImpCasts();
  }
  const QualType type = expr->getType();
  if (type->isPointerType()) {
    return type->getPointeeType();
  }
  if (const clang::ArrayType *array = ctx.getAsArrayType(type)) {
    return array->getElementType();
  }
  return {};
}

bool allowed(const clang::Stmt *stmt, clang::ASTContext &ctx, Reporter &reporter) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*stmt);
  for (int depth = 0; depth < 32; ++depth) {
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

} // namespace

void MemcmpStringCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("memcmp")))).bind("call"), this);
}

void MemcmpStringCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  if (!call || call->getNumArgs() < 2 || !result.Context || !result.SourceManager) {
    return;
  }
  if (allowed(call, *result.Context, reporter)) {
    return;
  }
  const QualType left = bufferElement(call->getArg(0), *result.Context);
  const QualType right = bufferElement(call->getArg(1), *result.Context);
  if (!isStringChar(left) && !isStringChar(right)) {
    return;
  }
  reporter.emit(*result.SourceManager, call->getBeginLoc(), kId, kMsg);
}

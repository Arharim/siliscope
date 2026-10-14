#include "siliscope/CtypeArg.h"

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

constexpr const char *kId = "ss.libc.ctype-uchar-or-eof";
constexpr const char *kMsg = "pass unsigned char or EOF to ctype";

bool isBuiltin(QualType type, clang::BuiltinType::Kind kind) {
  if (type.isNull()) {
    return false;
  }
  const auto *builtin = type->getAs<clang::BuiltinType>();
  return builtin && builtin->getKind() == kind;
}

bool isPlainChar(QualType type) {
  return isBuiltin(type, clang::BuiltinType::Char_U) || isBuiltin(type, clang::BuiltinType::Char_S);
}

bool isUnsignedChar(QualType type) {
  return isBuiltin(type, clang::BuiltinType::UChar);
}

bool isSignedChar(QualType type) {
  return isBuiltin(type, clang::BuiltinType::SChar);
}

// EOF is -1 on the libcs this target ships. Any other negative value is
// outside the unsigned char range those functions accept.
bool valueFits(const Expr *expr, clang::ASTContext &ctx) {
  clang::Expr::EvalResult value;
  if (!expr->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
    return false;
  }
  const llvm::APSInt number = value.Val.getInt();
  if (number == -1) {
    return true;
  }
  if (number.isNegative()) {
    return false;
  }
  return number.getActiveBits() <= ctx.getIntWidth(ctx.UnsignedCharTy);
}

bool castToUnsignedChar(const ExplicitCastExpr *cast, clang::ASTContext &ctx) {
  const QualType dest = cast->getType();
  if (isUnsignedChar(dest)) {
    return true;
  }
  // Plain char is an unsigned character type when the target says so.
  return isPlainChar(dest) && !ctx.getLangOpts().CharIsSigned;
}

// The parameter type is int, because that is where EOF lives. The defect is
// a signed character, whose high bit becomes a negative value.
bool safeArgument(const Expr *arg, clang::ASTContext &ctx) {
  const Expr *expr = arg->IgnoreParenImpCasts();
  if (!expr || expr->isTypeDependent()) {
    return true;
  }
  if (valueFits(expr, ctx)) {
    return true;
  }
  while (const auto *cast = llvm::dyn_cast<ExplicitCastExpr>(expr)) {
    if (castToUnsignedChar(cast, ctx)) {
      return true;
    }
    expr = cast->getSubExpr()->IgnoreParenImpCasts();
    if (!expr || valueFits(expr, ctx)) {
      return true;
    }
  }
  const QualType type = expr->getType();
  if (isUnsignedChar(type)) {
    return true;
  }
  if (isPlainChar(type) && !ctx.getLangOpts().CharIsSigned) {
    return true;
  }
  if (isSignedChar(type)) {
    return false;
  }
  if (isPlainChar(type)) {
    return false;
  }
  return true;
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

void CtypeArgCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("isalnum",
                                                            "isalpha",
                                                            "isblank",
                                                            "iscntrl",
                                                            "isdigit",
                                                            "isgraph",
                                                            "islower",
                                                            "isprint",
                                                            "ispunct",
                                                            "isspace",
                                                            "isupper",
                                                            "isxdigit",
                                                            "tolower",
                                                            "toupper"))))
                        .bind("call"),
                    this);
}

void CtypeArgCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  if (!call || call->getNumArgs() != 1 || !result.Context || !result.SourceManager) {
    return;
  }
  if (allowed(call, *result.Context, reporter)) {
    return;
  }
  if (safeArgument(call->getArg(0), *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager, call->getBeginLoc(), kId, kMsg);
}

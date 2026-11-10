#include "siliscope/LiteralSuffixCase.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringExtras.h"

#include <string>

using clang::Expr;
using clang::FloatingLiteral;
using clang::FunctionDecl;
using clang::IntegerLiteral;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Token;
using clang::ast_matchers::floatLiteral;
using clang::ast_matchers::integerLiteral;

namespace {

constexpr const char *kId = "ss.expr.literal-suffix-case";
constexpr const char *kMsg = "spell this literal suffix in uppercase";

bool project(const SourceManager &sm, SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  return loc.isValid() && !sm.isInSystemHeader(loc) && !sm.isWrittenInBuiltinFile(loc) &&
         !sm.isWrittenInScratchSpace(loc) && !sm.isInPredefinedFile(loc) &&
         !sm.isWrittenInCommandLineFile(loc);
}

bool lowerSuffix(llvm::StringRef spelling) {
  size_t end = spelling.size();
  while (end > 0 && llvm::isAlpha(spelling[end - 1])) {
    --end;
  }
  if (end == spelling.size()) {
    return false;
  }
  for (size_t i = end; i < spelling.size(); ++i) {
    const char c = spelling[i];
    if (c >= 'a' && c <= 'z') {
      return true;
    }
  }
  return false;
}

bool allowed(const Expr *expr, clang::ASTContext &ctx, Reporter &reporter) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*expr);
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

void check(const Expr *expr,
           const SourceManager &sm,
           const clang::LangOptions &lang,
           clang::ASTContext &ctx,
           Reporter &reporter) {
  if (!expr || !project(sm, expr->getBeginLoc()) || allowed(expr, ctx, reporter)) {
    return;
  }
  Token tok;
  const SourceLocation spell = sm.getSpellingLoc(expr->getBeginLoc());
  if (clang::Lexer::getRawToken(spell, tok, sm, lang, /*IgnoreWhiteSpace=*/true)) {
    return;
  }
  if (lowerSuffix(clang::Lexer::getSpelling(tok, sm, lang))) {
    reporter.emit(sm, spell, kId, kMsg);
  }
}

} // namespace

void LiteralSuffixCaseCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(integerLiteral().bind("int"), this);
  finder.addMatcher(floatLiteral().bind("float"), this);
}

void LiteralSuffixCaseCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  const clang::LangOptions &lang = result.Context->getLangOpts();
  if (const auto *lit = result.Nodes.getNodeAs<IntegerLiteral>("int")) {
    check(lit, sm, lang, *result.Context, reporter);
  } else if (const auto *lit = result.Nodes.getNodeAs<FloatingLiteral>("float")) {
    check(lit, sm, lang, *result.Context, reporter);
  }
}

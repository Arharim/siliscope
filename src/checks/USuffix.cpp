#include "siliscope/USuffix.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

using clang::Expr;
using clang::ImplicitCastExpr;
using clang::IntegerLiteral;
using clang::Lexer;
using clang::ParenExpr;
using clang::ast_matchers::integerLiteral;

namespace {

static bool spellingHasU(const IntegerLiteral *lit,
                         const clang::SourceManager &sm,
                         const clang::LangOptions &lang) {
  // The expansion location is the macro use, whose text has no suffix.
  // The suffix is on the token in the definition: 256u, 0xE000EDFCu.
  llvm::SmallString<32> buf;
  const clang::SourceLocation spelled = sm.getSpellingLoc(lit->getLocation());
  const llvm::StringRef spelling = Lexer::getSpelling(spelled, buf, sm, lang);
  return spelling.contains('u') || spelling.contains('U');
}

// A U suffix changes the type of the literal. That matters for unsigned int
// and wider. unsigned char / plain char (unsigned on ARM) still store the
// same small value either way, and flagging buf[i] = 0 drowns firmware.
static bool wideUnsigned(clang::QualType t, clang::ASTContext &ctx) {
  if (t.isNull() || !t->isUnsignedIntegerType()) {
    return false;
  }
  return ctx.getIntWidth(t) >= ctx.getIntWidth(ctx.UnsignedIntTy);
}

static bool convertedToUnsigned(const IntegerLiteral *lit, clang::ASTContext &ctx) {
  const Expr *e = lit;
  for (int i = 0; i < 8; ++i) {
    const auto parents = ctx.getParents(*e);
    if (parents.empty()) {
      return false;
    }
    if (const auto *paren = parents[0].get<ParenExpr>()) {
      e = paren;
      continue;
    }
    const auto *cast = parents[0].get<ImplicitCastExpr>();
    if (!cast) {
      return false;
    }
    return wideUnsigned(cast->getType(), ctx);
  }
  return false;
}

} // namespace

void USuffixCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(integerLiteral().bind("lit"), this);
}

void USuffixCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *lit = result.Nodes.getNodeAs<IntegerLiteral>("lit");
  if (!lit || !result.Context || !result.SourceManager) {
    return;
  }
  const bool unsignedLiteral = wideUnsigned(lit->getType(), *result.Context);
  if (!unsignedLiteral && !convertedToUnsigned(lit, *result.Context)) {
    return;
  }
  if (spellingHasU(lit, *result.SourceManager, result.Context->getLangOpts())) {
    return;
  }
  reporter.emit(*result.SourceManager,
                lit->getLocation(),
                "ss.conv.u-suffix",
                "unsigned context needs a U suffix");
}

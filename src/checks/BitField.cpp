#include "siliscope/BitField.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <string>

using clang::FieldDecl;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::isBitField;

namespace {

constexpr const char *kId = "ss.type.bitfield";
constexpr const char *kPlain = "give this bit-field an explicit signed or unsigned type";
constexpr const char *kOne = "a single-bit field shall not be signed";

bool allowed(const FieldDecl *field, Reporter &reporter) {
  if (!field || field->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = field->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

// "unsigned" contains "signed", so the unsigned spelling wins.
void spelledSign(const FieldDecl *field,
                 const clang::SourceManager &sm,
                 const clang::LangOptions &lang,
                 bool &isUnsigned,
                 bool &isSigned) {
  isUnsigned = false;
  isSigned = false;
  if (!field->getTypeSourceInfo()) {
    return;
  }
  const clang::SourceLocation begin = field->getTypeSourceInfo()->getTypeLoc().getBeginLoc();
  const clang::SourceLocation end = field->getLocation();
  if (begin.isInvalid() || end.isInvalid() || !sm.isBeforeInTranslationUnit(begin, end)) {
    return;
  }
  const llvm::StringRef text =
      clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(begin, end), sm, lang);
  isUnsigned = text.find("unsigned") != llvm::StringRef::npos;
  isSigned = !isUnsigned && text.find("signed") != llvm::StringRef::npos;
}

} // namespace

void BitFieldCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(fieldDecl(isBitField()).bind("field"), this);
}

void BitFieldCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *field = result.Nodes.getNodeAs<FieldDecl>("field");
  if (!field || !result.Context || !result.SourceManager || allowed(field, reporter)) {
    return;
  }
  const clang::QualType type = field->getType();
  if (type->isBooleanType() || type->isUnsignedIntegerType() || type->isDependentType()) {
    return;
  }
  bool isUnsigned = false;
  bool isSigned = false;
  spelledSign(field, *result.SourceManager, result.Context->getLangOpts(), isUnsigned, isSigned);
  if (isUnsigned) {
    return;
  }
  unsigned width = 0;
  bool known = false;
  if (const clang::Expr *bits = field->getBitWidth()) {
    clang::Expr::EvalResult value;
    if (bits->EvaluateAsInt(value, *result.Context) && value.Val.isInt()) {
      known = true;
      width = value.Val.getInt().getZExtValue();
    }
  }
  const auto *builtin = type->getAs<clang::BuiltinType>();
  const bool plainInt = !isSigned && builtin && builtin->getKind() == clang::BuiltinType::Int;
  if (plainInt) {
    reporter.emit(*result.SourceManager, field->getLocation(), kId, kPlain);
  }
  if (known && width == 1) {
    reporter.emit(*result.SourceManager, field->getLocation(), kId, kOne);
  }
}

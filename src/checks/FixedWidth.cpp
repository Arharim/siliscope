#include "siliscope/FixedWidth.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::BinaryOperator;
using clang::CharacterLiteral;
using clang::DeclaratorDecl;
using clang::Expr;
using clang::FunctionDecl;
using clang::QualType;
using clang::UnaryOperator;
using clang::ValueDecl;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::declaratorDecl;
using clang::ast_matchers::typedefDecl;
using clang::ast_matchers::unaryOperator;

namespace {

constexpr const char *kId = "ss.type.fixed-width";
constexpr const char *kWidth = "use a fixed-width integer instead of short or long";
constexpr const char *kChar = "keep char for character data";

// A typedef is the stdint name, even when the target spells int64_t as
// long long. The written short or long is the defect.
bool peelSugar(QualType &type) {
  while (!type.isNull()) {
    const clang::Type *inner = type.getTypePtr();
    if (const auto *paren = llvm::dyn_cast<clang::ParenType>(inner)) {
      type = paren->getInnerType();
      continue;
    }
    if (llvm::isa<clang::TypedefType>(inner)) {
      return false;
    }
    return true;
  }
  return false;
}

bool isShortOrLong(QualType type) {
  while (peelSugar(type)) {
    const clang::Type *inner = type.getTypePtr();
    if (const auto *pointer = llvm::dyn_cast<clang::PointerType>(inner)) {
      type = pointer->getPointeeType();
      continue;
    }
    if (const auto *array = llvm::dyn_cast<clang::ArrayType>(inner)) {
      type = array->getElementType();
      continue;
    }
    if (const auto *reference = llvm::dyn_cast<clang::ReferenceType>(inner)) {
      type = reference->getPointeeType();
      continue;
    }
    const auto *builtin = llvm::dyn_cast<clang::BuiltinType>(inner);
    if (!builtin) {
      return false;
    }
    switch (builtin->getKind()) {
    case clang::BuiltinType::Short:
    case clang::BuiltinType::Long:
    case clang::BuiltinType::LongLong:
    case clang::BuiltinType::UShort:
    case clang::BuiltinType::ULong:
    case clang::BuiltinType::ULongLong:
      return true;
    default:
      return false;
    }
  }
  return false;
}

bool isPlainChar(QualType type) {
  if (!peelSugar(type)) {
    return false;
  }
  const auto *builtin = llvm::dyn_cast<clang::BuiltinType>(type.getTypePtr());
  return builtin && (builtin->getKind() == clang::BuiltinType::Char_U ||
                     builtin->getKind() == clang::BuiltinType::Char_S);
}

bool allowed(const clang::NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool numeric(clang::BinaryOperator::Opcode op) {
  switch (op) {
  case clang::BO_Add:
  case clang::BO_Sub:
  case clang::BO_Mul:
  case clang::BO_Div:
  case clang::BO_Rem:
  case clang::BO_Shl:
  case clang::BO_Shr:
  case clang::BO_And:
  case clang::BO_Xor:
  case clang::BO_Or:
  case clang::BO_AddAssign:
  case clang::BO_SubAssign:
  case clang::BO_MulAssign:
  case clang::BO_DivAssign:
  case clang::BO_RemAssign:
  case clang::BO_ShlAssign:
  case clang::BO_ShrAssign:
  case clang::BO_AndAssign:
  case clang::BO_XorAssign:
  case clang::BO_OrAssign:
    return true;
  default:
    return false;
  }
}

const clang::NamedDecl *ownerOf(const Expr *expr, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*expr);
  for (int depth = 0; depth < 32; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      return fn;
    }
  }
  return nullptr;
}

} // namespace

void FixedWidthCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(declaratorDecl().bind("decl"), this);
  finder.addMatcher(typedefDecl().bind("typedef"), this);
  finder.addMatcher(unaryOperator().bind("unary"), this);
  finder.addMatcher(binaryOperator().bind("binary"), this);
}

void FixedWidthCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager) {
    return;
  }
  if (const auto *decl = result.Nodes.getNodeAs<DeclaratorDecl>("decl")) {
    if (decl->isImplicit() || allowed(decl, reporter)) {
      return;
    }
    QualType type = decl->getType();
    if (const auto *fn = llvm::dyn_cast<FunctionDecl>(decl)) {
      type = fn->getReturnType();
    }
    if (isShortOrLong(type)) {
      reporter.emit(*result.SourceManager, decl->getLocation(), kId, kWidth);
    }
    return;
  }
  if (const auto *alias = result.Nodes.getNodeAs<clang::TypedefNameDecl>("typedef")) {
    if (alias->isImplicit() || allowed(alias, reporter) ||
        !isShortOrLong(alias->getUnderlyingType())) {
      return;
    }
    reporter.emit(*result.SourceManager, alias->getLocation(), kId, kWidth);
    return;
  }
  if (!result.Context) {
    return;
  }
  if (const auto *unary = result.Nodes.getNodeAs<UnaryOperator>("unary")) {
    switch (unary->getOpcode()) {
    case clang::UO_PostInc:
    case clang::UO_PostDec:
    case clang::UO_PreInc:
    case clang::UO_PreDec:
    case clang::UO_Minus:
    case clang::UO_Not:
      break;
    default:
      return;
    }
    const Expr *operand = unary->getSubExpr()->IgnoreParenImpCasts();
    if (!isPlainChar(operand->getType()) || llvm::isa<CharacterLiteral>(operand)) {
      return;
    }
    if (allowed(ownerOf(unary, *result.Context), reporter)) {
      return;
    }
    reporter.emit(*result.SourceManager, unary->getOperatorLoc(), kId, kChar);
    return;
  }
  const auto *binary = result.Nodes.getNodeAs<BinaryOperator>("binary");
  if (!binary || !numeric(binary->getOpcode())) {
    return;
  }
  const Expr *left = binary->getLHS()->IgnoreParenImpCasts();
  const Expr *right = binary->getRHS()->IgnoreParenImpCasts();
  if (llvm::isa<CharacterLiteral>(left) || llvm::isa<CharacterLiteral>(right)) {
    return;
  }
  if (!isPlainChar(left->getType()) && !isPlainChar(right->getType())) {
    return;
  }
  if (allowed(ownerOf(binary, *result.Context), reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, binary->getOperatorLoc(), kId, kChar);
}

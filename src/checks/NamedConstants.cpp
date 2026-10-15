#include "siliscope/NamedConstants.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::ASTContext;
using clang::DeclaratorDecl;
using clang::DesignatedInitExpr;
using clang::DoStmt;
using clang::DynTypedNode;
using clang::Expr;
using clang::FieldDecl;
using clang::FloatingLiteral;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::InitListExpr;
using clang::IntegerLiteral;
using clang::NamedDecl;
using clang::QualType;
using clang::RecordDecl;
using clang::SourceManager;
using clang::TypeLoc;
using clang::ValueDecl;
using clang::VarDecl;
using clang::WhileStmt;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::conditionalOperator;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::ifStmt;
using clang::ast_matchers::initListExpr;
using clang::ast_matchers::valueDecl;
using clang::ast_matchers::whileStmt;

namespace {

constexpr const char *kId = "ss.expr.named-constants";
constexpr const char *kMsg = "name this numeric literal";

bool allowed(const NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

const NamedDecl *ownerOf(const Expr *expr, ASTContext &ctx) {
  DynTypedNode node = DynTypedNode::create(*expr);
  const NamedDecl *var = nullptr;
  for (int step = 0; step < 32; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      break;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      return fn;
    }
    if (!var) {
      if (const auto *found = node.get<VarDecl>()) {
        var = found;
      }
    }
  }
  return var;
}

bool macroBody(const Expr *expr, const SourceManager &sm) {
  return expr && sm.isMacroBodyExpansion(expr->getBeginLoc());
}

bool plainInt(const IntegerLiteral *lit) {
  const llvm::APInt &value = lit->getValue();
  return value.isZero() || value.isOne();
}

bool plainFloat(const FloatingLiteral *lit) {
  const llvm::APFloat &value = lit->getValue();
  if (value.isZero()) {
    return true;
  }
  llvm::APFloat one(value.getSemantics(), 1);
  if (value.compare(one) == llvm::APFloat::cmpEqual) {
    return true;
  }
  one.changeSign();
  return value.compare(one) == llvm::APFloat::cmpEqual;
}

class LiteralScan : public clang::RecursiveASTVisitor<LiteralScan> {
public:
  LiteralScan(const SourceManager &sm, Reporter &reporter) : sm(sm), reporter(reporter) {}

  bool TraverseUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr *) { return true; }

  bool VisitIntegerLiteral(IntegerLiteral *lit) {
    if (!lit || plainInt(lit) || macroBody(lit, sm)) {
      return true;
    }
    reporter.emit(sm, lit->getBeginLoc(), kId, kMsg);
    return true;
  }

  bool VisitFloatingLiteral(FloatingLiteral *lit) {
    if (!lit || plainFloat(lit) || macroBody(lit, sm)) {
      return true;
    }
    reporter.emit(sm, lit->getBeginLoc(), kId, kMsg);
    return true;
  }

private:
  const SourceManager &sm;
  Reporter &reporter;
};

void scan(const Expr *expr, const SourceManager &sm, Reporter &reporter) {
  if (!expr) {
    return;
  }
  LiteralScan walk(sm, reporter);
  walk.TraverseStmt(const_cast<Expr *>(expr));
}

bool packed(QualType type) {
  if (type.isNull()) {
    return false;
  }
  const auto *record = type->getAs<clang::RecordType>();
  if (!record || !record->getDecl()) {
    return false;
  }
  const auto *decl = llvm::cast<RecordDecl>(record->getDecl()->getCanonicalDecl());
  return decl->hasAttr<clang::PackedAttr>();
}

void walkArray(TypeLoc loc, const SourceManager &sm, Reporter &reporter) {
  for (int step = 0; step < 8 && !loc.isNull(); ++step) {
    if (auto array = loc.getAs<clang::ConstantArrayTypeLoc>()) {
      scan(array.getSizeExpr(), sm, reporter);
      loc = array.getElementLoc();
      continue;
    }
    if (auto paren = loc.getAs<clang::ParenTypeLoc>()) {
      loc = paren.getInnerLoc();
      continue;
    }
    if (auto qual = loc.getAs<clang::QualifiedTypeLoc>()) {
      loc = qual.getUnqualifiedLoc();
      continue;
    }
    if (auto attr = loc.getAs<clang::AttributedTypeLoc>()) {
      loc = attr.getModifiedLoc();
      continue;
    }
    break;
  }
}

bool delayName(const clang::FunctionDecl *fn) {
  // Constructors and overloaded operators are not identifiers. getName() asserts.
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const llvm::StringRef name = fn->getName();
  return name.contains_insensitive("delay") || name.contains_insensitive("sleep");
}

} // namespace

void NamedConstantsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(ifStmt().bind("if"), this);
  finder.addMatcher(whileStmt().bind("while"), this);
  finder.addMatcher(doStmt().bind("do"), this);
  finder.addMatcher(forStmt().bind("for"), this);
  finder.addMatcher(conditionalOperator().bind("tern"), this);
  finder.addMatcher(valueDecl().bind("decl"), this);
  finder.addMatcher(initListExpr().bind("init"), this);
  finder.addMatcher(callExpr().bind("call"), this);
}

void NamedConstantsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  ASTContext &ctx = *result.Context;

  if (const auto *stmt = result.Nodes.getNodeAs<IfStmt>("if")) {
    if (!allowed(ownerOf(stmt->getCond(), ctx), reporter)) {
      scan(stmt->getCond(), sm, reporter);
    }
    return;
  }
  if (const auto *stmt = result.Nodes.getNodeAs<WhileStmt>("while")) {
    if (!allowed(ownerOf(stmt->getCond(), ctx), reporter)) {
      scan(stmt->getCond(), sm, reporter);
    }
    return;
  }
  if (const auto *stmt = result.Nodes.getNodeAs<DoStmt>("do")) {
    if (!allowed(ownerOf(stmt->getCond(), ctx), reporter)) {
      scan(stmt->getCond(), sm, reporter);
    }
    return;
  }
  if (const auto *stmt = result.Nodes.getNodeAs<ForStmt>("for")) {
    if (stmt->getCond() && !allowed(ownerOf(stmt->getCond(), ctx), reporter)) {
      scan(stmt->getCond(), sm, reporter);
    }
    return;
  }
  if (const auto *tern = result.Nodes.getNodeAs<clang::ConditionalOperator>("tern")) {
    if (!allowed(ownerOf(tern->getCond(), ctx), reporter)) {
      scan(tern->getCond(), sm, reporter);
    }
    return;
  }
  if (const auto *decl = result.Nodes.getNodeAs<ValueDecl>("decl")) {
    const auto *declarator = llvm::dyn_cast<DeclaratorDecl>(decl);
    if (declarator && declarator->getTypeSourceInfo() && !allowed(decl, reporter)) {
      walkArray(declarator->getTypeSourceInfo()->getTypeLoc(), sm, reporter);
    }
    return;
  }
  if (const auto *init = result.Nodes.getNodeAs<InitListExpr>("init")) {
    // The matched node is usually the semantic form. Designators live on the
    // syntactic form, which is not a separate child of the declaration.
    const InitListExpr *syn = init->getSyntacticForm() ? init->getSyntacticForm() : init;
    const InitListExpr *typed =
        init->isSemanticForm() ? init : (init->getSemanticForm() ? init->getSemanticForm() : syn);
    const NamedDecl *owner = nullptr;
    const Expr *seed = init->getNumInits() > 0 ? init->getInit(0) : nullptr;
    if (!seed && syn->getNumInits() > 0) {
      seed = syn->getInit(0);
    }
    if (seed) {
      owner = ownerOf(seed, ctx);
    }
    if (allowed(owner, reporter)) {
      return;
    }
    const bool isPacked = packed(typed->getType());
    for (const Expr *kid : syn->inits()) {
      const auto *designated = llvm::dyn_cast_or_null<DesignatedInitExpr>(kid);
      if (!designated) {
        if (isPacked) {
          scan(kid, sm, reporter);
        }
        continue;
      }
      bool field = false;
      for (const DesignatedInitExpr::Designator &design : designated->designators()) {
        if (design.isFieldDesignator()) {
          field = true;
        }
      }
      if (field) {
        scan(designated->getInit(), sm, reporter);
      }
    }
    return;
  }
  if (const auto *call = result.Nodes.getNodeAs<clang::CallExpr>("call")) {
    if (!delayName(call->getDirectCallee()) || allowed(ownerOf(call, ctx), reporter)) {
      return;
    }
    for (const Expr *arg : call->arguments()) {
      scan(arg, sm, reporter);
    }
  }
}

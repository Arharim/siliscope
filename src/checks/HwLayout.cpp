#include "siliscope/HwLayout.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <set>
#include <string>
#include <vector>

using clang::Expr;
using clang::FieldDecl;
using clang::QualType;
using clang::RecordDecl;
using clang::SourceManager;
using clang::StaticAssertDecl;
using clang::TranslationUnitDecl;
using clang::UnaryExprOrTypeTraitExpr;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.type.hw-layout";
constexpr const char *kMsg = "give this hardware struct a defined layout";

std::string recordName(const RecordDecl *decl) {
  if (decl->getDeclName().isIdentifier() && !decl->getName().empty()) {
    return decl->getName().str();
  }
  if (const auto *alias = decl->getTypedefNameForAnonDecl()) {
    if (alias->getDeclName().isIdentifier()) {
      return alias->getName().str();
    }
  }
  return {};
}

bool allowed(const RecordDecl *decl, Reporter &reporter) {
  const std::string name = recordName(decl);
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool hwName(llvm::StringRef name) {
  return name.contains_insensitive("reg") || name.contains_insensitive("dma") ||
         name.contains_insensitive("packet") || name.contains_insensitive("proto") ||
         name.contains_insensitive("wire") || name.contains_insensitive("mailbox") ||
         name.contains_insensitive("descriptor") || name.contains_insensitive("frame");
}

bool volatileField(const RecordDecl *decl) {
  for (const FieldDecl *field : decl->fields()) {
    if (field && field->getType().isVolatileQualified()) {
      return true;
    }
  }
  return false;
}

const RecordDecl *recordOf(QualType type) {
  if (type.isNull()) {
    return nullptr;
  }
  const auto *record = type->getAs<clang::RecordType>();
  if (!record || !record->getDecl()) {
    return nullptr;
  }
  return llvm::cast<RecordDecl>(record->getDecl()->getCanonicalDecl());
}

class NoteSize : public clang::RecursiveASTVisitor<NoteSize> {
public:
  std::set<const RecordDecl *> sizes;

  bool VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *expr) {
    if (!expr || expr->getKind() != clang::UETT_SizeOf) {
      return true;
    }
    const QualType type =
        expr->isArgumentType() ? expr->getArgumentType() : expr->getArgumentExpr()->getType();
    if (const RecordDecl *record = recordOf(type)) {
      sizes.insert(record);
    }
    return true;
  }
};

class Collect : public clang::RecursiveASTVisitor<Collect> {
public:
  std::vector<const RecordDecl *> records;
  std::vector<const Expr *> asserts;

  bool VisitRecordDecl(RecordDecl *decl) {
    if (!decl || !decl->isThisDeclarationADefinition() || decl->isImplicit() || decl->isUnion() ||
        decl->isLambda() || decl->isDependentContext()) {
      return true;
    }
    records.push_back(decl);
    return true;
  }

  bool VisitStaticAssertDecl(StaticAssertDecl *decl) {
    if (decl && decl->getAssertExpr()) {
      asserts.push_back(decl->getAssertExpr());
    }
    return true;
  }
};

bool inSystem(const RecordDecl *decl, const SourceManager &sm) {
  const clang::SourceLocation loc = sm.getSpellingLoc(decl->getLocation());
  return loc.isInvalid() || sm.isInSystemHeader(loc);
}

} // namespace

void HwLayoutCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void HwLayoutCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  Collect collect;
  collect.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
  NoteSize sizes;
  for (const Expr *expr : collect.asserts) {
    sizes.TraverseStmt(const_cast<Expr *>(expr));
  }
  std::set<const RecordDecl *> seen;
  for (const RecordDecl *decl : collect.records) {
    const auto *canon = llvm::cast<RecordDecl>(decl->getCanonicalDecl());
    if (!seen.insert(canon).second || inSystem(decl, *result.SourceManager) ||
        allowed(decl, reporter)) {
      continue;
    }
    const std::string name = recordName(decl);
    if (!volatileField(decl) && (name.empty() || !hwName(name))) {
      continue;
    }
    if (decl->hasAttr<clang::PackedAttr>() || sizes.sizes.count(canon)) {
      continue;
    }
    reporter.emit(*result.SourceManager, decl->getLocation(), kId, kMsg);
  }
}

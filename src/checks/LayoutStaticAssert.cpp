#include "siliscope/LayoutStaticAssert.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>
#include <utility>
#include <vector>

using clang::Expr;
using clang::FieldDecl;
using clang::OffsetOfExpr;
using clang::QualType;
using clang::RecordDecl;
using clang::StaticAssertDecl;
using clang::UnaryExprOrTypeTraitExpr;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.emb.layout-static-assert";
constexpr const char *kSize = "assert the size of this packed struct";
constexpr const char *kOffset = "assert the offset of this field";

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

bool allowed(const RecordDecl *decl, Reporter &reporter) {
  if (!decl || !decl->getIdentifier()) {
    return false;
  }
  const std::string name = decl->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

class Collect : public clang::RecursiveASTVisitor<Collect> {
public:
  std::vector<const RecordDecl *> packed;
  std::vector<const Expr *> asserts;

  bool VisitRecordDecl(RecordDecl *decl) {
    if (!decl || !decl->isThisDeclarationADefinition() || decl->isImplicit() || decl->isUnion() ||
        !decl->hasAttr<clang::PackedAttr>()) {
      return true;
    }
    packed.push_back(decl);
    return true;
  }

  bool VisitStaticAssertDecl(StaticAssertDecl *decl) {
    if (decl && decl->getAssertExpr()) {
      asserts.push_back(decl->getAssertExpr());
    }
    return true;
  }
};

class NoteLayout : public clang::RecursiveASTVisitor<NoteLayout> {
public:
  std::vector<const RecordDecl *> sizes;
  std::vector<std::pair<const RecordDecl *, const FieldDecl *>> offsets;

  bool VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *expr) {
    if (!expr || expr->getKind() != clang::UETT_SizeOf) {
      return true;
    }
    QualType type =
        expr->isArgumentType() ? expr->getArgumentType() : expr->getArgumentExpr()->getType();
    if (const RecordDecl *record = recordOf(type)) {
      sizes.push_back(record);
    }
    return true;
  }

  bool VisitOffsetOfExpr(OffsetOfExpr *expr) {
    if (!expr || !expr->getTypeSourceInfo()) {
      return true;
    }
    const RecordDecl *record = recordOf(expr->getTypeSourceInfo()->getType());
    if (!record) {
      return true;
    }
    for (unsigned i = 0; i < expr->getNumComponents(); ++i) {
      const clang::OffsetOfNode node = expr->getComponent(i);
      if (node.getKind() != clang::OffsetOfNode::Field || !node.getField()) {
        continue;
      }
      offsets.emplace_back(record, node.getField()->getCanonicalDecl());
      break;
    }
    return true;
  }
};

bool notedSize(const std::vector<const RecordDecl *> &sizes, const RecordDecl *record) {
  const auto *canon = llvm::cast<RecordDecl>(record->getCanonicalDecl());
  for (const RecordDecl *item : sizes) {
    if (item == canon) {
      return true;
    }
  }
  return false;
}

bool notedOffset(const std::vector<std::pair<const RecordDecl *, const FieldDecl *>> &offsets,
                 const RecordDecl *record,
                 const FieldDecl *field) {
  const auto *canon = llvm::cast<RecordDecl>(record->getCanonicalDecl());
  const FieldDecl *want = field->getCanonicalDecl();
  for (const auto &item : offsets) {
    if (item.first == canon && item.second == want) {
      return true;
    }
  }
  return false;
}

} // namespace

void LayoutStaticAssertCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void LayoutStaticAssertCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  Collect collect;
  collect.TraverseDecl(const_cast<clang::TranslationUnitDecl *>(tu));
  NoteLayout notes;
  for (const Expr *expr : collect.asserts) {
    notes.TraverseStmt(const_cast<Expr *>(expr));
  }
  for (const RecordDecl *record : collect.packed) {
    if (allowed(record, reporter)) {
      continue;
    }
    if (!notedSize(notes.sizes, record)) {
      reporter.emit(*result.SourceManager, record->getLocation(), kId, kSize);
    }
    for (const FieldDecl *field : record->fields()) {
      if (!field || field->isBitField() || !field->getIdentifier()) {
        continue;
      }
      if (notedOffset(notes.offsets, record, field)) {
        continue;
      }
      reporter.emit(*result.SourceManager, field->getLocation(), kId, kOffset);
    }
  }
}

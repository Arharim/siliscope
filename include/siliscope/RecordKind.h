#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"

// Public data and friend are allowed on a POD, a packed record, or a union.
// A dependent pattern is left to its instantiations.
inline bool allowsPublicSurface(const clang::CXXRecordDecl *rec, const clang::ASTContext &ctx) {
  if (!rec) {
    return false;
  }
  const clang::CXXRecordDecl *def = rec->getDefinition();
  if (!def) {
    def = rec;
  }
  if (def->isUnion() || def->hasAttr<clang::PackedAttr>()) {
    return true;
  }
  if (def->isDependentContext() || !def->isCompleteDefinition()) {
    return true;
  }
  return clang::QualType(ctx.getCanonicalTagType(def)).isCXX11PODType(ctx);
}

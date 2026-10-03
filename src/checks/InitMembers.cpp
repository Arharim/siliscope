#include "siliscope/InitMembers.h"

#include "siliscope/Report.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <string>

using clang::ASTContext;
using clang::CXXBaseSpecifier;
using clang::CXXConstructorDecl;
using clang::CXXCtorInitializer;
using clang::CXXRecordDecl;
using clang::FieldDecl;
using clang::QualType;
using clang::SourceLocation;
using clang::ast_matchers::cxxConstructorDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.cpp.init-members";

// A trivial default constructor leaves scalar subobjects indeterminate.
// A non-trivial one runs and is checked on its own when it is user-written.
bool trivialDefaultLeavesGarbage(const CXXRecordDecl *rec) {
  const CXXRecordDecl *def = rec ? rec->getDefinition() : nullptr;
  if (!def || def->isInvalidDecl() || def->isDependentContext()) {
    return false;
  }
  if (def->hasNonTrivialDefaultConstructor()) {
    return false;
  }
  return def->hasTrivialDefaultConstructor() || def->needsImplicitDefaultConstructor();
}

bool needsMemInitializer(ASTContext &ctx, QualType type) {
  if (type.isNull() || type->isDependentType()) {
    return false;
  }
  if (type->isReferenceType()) {
    return true;
  }
  if (type->isArrayType()) {
    type = ctx.getBaseElementType(type);
  }
  if (const auto *rec = type->getAsCXXRecordDecl()) {
    return trivialDefaultLeavesGarbage(rec);
  }
  return type->isScalarType();
}

bool memberInitialized(const CXXConstructorDecl *ctor, const FieldDecl *field) {
  if (field->hasInClassInitializer()) {
    return true;
  }
  const FieldDecl *canon = field->getCanonicalDecl();
  for (const CXXCtorInitializer *init : ctor->inits()) {
    if (!init->isAnyMemberInitializer() || init->isInClassMemberInitializer()) {
      continue;
    }
    const FieldDecl *member = init->getAnyMember();
    if (member && member->getCanonicalDecl() == canon) {
      return true;
    }
  }
  return false;
}

bool baseWritten(ASTContext &ctx, const CXXConstructorDecl *ctor, QualType baseType) {
  for (const CXXCtorInitializer *init : ctor->inits()) {
    if (!init->isWritten() || !init->isBaseInitializer() || !init->getBaseClass()) {
      continue;
    }
    if (ctx.hasSameUnqualifiedType(baseType, QualType(init->getBaseClass(), 0))) {
      return true;
    }
  }
  return false;
}

std::string memberLabel(const FieldDecl *field) {
  if (field->getIdentifier()) {
    return field->getName().str();
  }
  return "member";
}

std::string baseLabel(const CXXBaseSpecifier &base) {
  if (const auto *rec = base.getType()->getAsCXXRecordDecl()) {
    if (rec->getIdentifier()) {
      return rec->getName().str();
    }
  }
  return "base";
}

void emitMissing(Reporter &reporter,
                 const clang::SourceManager &sm,
                 SourceLocation loc,
                 const std::string &what) {
  const std::string msg = "constructor does not initialize '" + what + "'";
  reporter.emit(sm, loc, kId, msg.c_str());
}

// Language initialization order: virtual bases, then direct non-virtual
// bases, then members. -1 if this written initializer is not one of those.
int initializationRank(ASTContext &ctx, const CXXRecordDecl *rec, const CXXCtorInitializer *init) {
  int rank = 0;
  if (init->isBaseInitializer() && init->getBaseClass()) {
    QualType want(init->getBaseClass(), 0);
    for (const CXXBaseSpecifier &base : rec->vbases()) {
      if (ctx.hasSameUnqualifiedType(want, base.getType())) {
        return rank;
      }
      ++rank;
    }
    for (const CXXBaseSpecifier &base : rec->bases()) {
      if (base.isVirtual()) {
        continue;
      }
      if (ctx.hasSameUnqualifiedType(want, base.getType())) {
        return rank;
      }
      ++rank;
    }
    return -1;
  }
  const FieldDecl *member = init->getAnyMember();
  if (!member) {
    return -1;
  }
  rank += static_cast<int>(rec->getNumVBases());
  for (const CXXBaseSpecifier &base : rec->bases()) {
    if (!base.isVirtual()) {
      ++rank;
    }
  }
  const FieldDecl *canon = member->getCanonicalDecl();
  for (const FieldDecl *field : rec->fields()) {
    if (field->isUnnamedBitField()) {
      continue;
    }
    if (field->getCanonicalDecl() == canon) {
      return rank;
    }
    ++rank;
  }
  return -1;
}

void checkMissing(Reporter &reporter,
                  const clang::SourceManager &sm,
                  ASTContext &ctx,
                  const CXXConstructorDecl *ctor,
                  const CXXRecordDecl *rec) {
  const SourceLocation loc = ctor->getLocation();
  if (rec->isUnion()) {
    bool any = false;
    const FieldDecl *first = nullptr;
    for (const FieldDecl *field : rec->fields()) {
      if (field->isUnnamedBitField()) {
        continue;
      }
      if (memberInitialized(ctor, field)) {
        any = true;
        break;
      }
      if (!first && needsMemInitializer(ctx, field->getType())) {
        first = field;
      }
    }
    // One active union member is enough. The others stay uninitialized.
    if (!any && first) {
      emitMissing(reporter, sm, loc, memberLabel(first));
    }
    return;
  }
  for (const CXXBaseSpecifier &base : rec->bases()) {
    // Only the most derived constructor initializes a virtual base. Requiring
    // it on an intermediate class would ask for an initializer the language ignores.
    if (base.isVirtual() || base.getType()->isDependentType()) {
      continue;
    }
    if (baseWritten(ctx, ctor, base.getType())) {
      continue;
    }
    const auto *baseRec = base.getType()->getAsCXXRecordDecl();
    if (!baseRec || !trivialDefaultLeavesGarbage(baseRec)) {
      continue;
    }
    emitMissing(reporter, sm, loc, baseLabel(base));
  }
  for (const FieldDecl *field : rec->fields()) {
    // The names inside an anonymous struct or union are not this field.
    if (field->isUnnamedBitField() || field->isAnonymousStructOrUnion()) {
      continue;
    }
    if (memberInitialized(ctor, field) || !needsMemInitializer(ctx, field->getType())) {
      continue;
    }
    emitMissing(reporter, sm, loc, memberLabel(field));
  }
}

void checkOrder(Reporter &reporter,
                const clang::SourceManager &sm,
                ASTContext &ctx,
                const CXXConstructorDecl *ctor,
                const CXXRecordDecl *rec) {
  struct Written {
    int source = 0;
    int rank = 0;
    const CXXCtorInitializer *init = nullptr;
  };
  llvm::SmallVector<Written, 8> written;
  for (const CXXCtorInitializer *init : ctor->inits()) {
    if (!init->isWritten() || init->getSourceOrder() < 0) {
      continue;
    }
    const int rank = initializationRank(ctx, rec, init);
    if (rank < 0) {
      continue;
    }
    written.push_back(Written{init->getSourceOrder(), rank, init});
  }
  std::stable_sort(written.begin(), written.end(), [](const Written &a, const Written &b) {
    return a.source < b.source;
  });
  for (size_t i = 1; i < written.size(); ++i) {
    if (written[i].rank >= written[i - 1].rank) {
      continue;
    }
    // Written earlier than a subobject that runs first.
    reporter.emit(sm,
                  written[i - 1].init->getSourceLocation(),
                  kId,
                  "initializer is not in declaration order");
  }
}

} // namespace

void InitMembersCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxConstructorDecl(isDefinition(), unless(isImplicit())).bind("ctor"), this);
}

void InitMembersCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *ctor = result.Nodes.getNodeAs<CXXConstructorDecl>("ctor");
  if (!ctor || ctor->isInvalidDecl() || ctor->isDeleted() || ctor->isDelegatingConstructor() ||
      ctor->isInheritingConstructor() || ctor->isTemplateInstantiation()) {
    return;
  }
  // A defaulted copy or move copies every member. A defaulted default
  // constructor does not, so an indeterminate scalar is still a miss.
  // Assignment in the body is not initialization.
  if (ctor->isDefaulted() && !ctor->isDefaultConstructor()) {
    return;
  }
  const CXXRecordDecl *rec = ctor->getParent() ? ctor->getParent()->getDefinition() : nullptr;
  if (!rec) {
    return;
  }
  checkMissing(reporter, *result.SourceManager, *result.Context, ctor, rec);
  checkOrder(reporter, *result.SourceManager, *result.Context, ctor, rec);
}

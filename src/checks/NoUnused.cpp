#include "siliscope/NoUnused.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"

#include <map>
#include <string>
#include <vector>

using clang::FunctionDecl;
using clang::NamedDecl;
using clang::TranslationUnitDecl;
using clang::TypedefNameDecl;
using clang::VarDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.decl.no-unused";

bool inMain(const clang::Decl *d, const clang::SourceManager &sm) {
  if (!d) {
    return false;
  }
  const clang::SourceLocation loc = d->getLocation();
  return loc.isValid() && sm.isWrittenInMainFile(sm.getSpellingLoc(loc));
}

bool kept(const NamedDecl *d) {
  const auto *canon = llvm::cast<NamedDecl>(d->getCanonicalDecl());
  return d->hasAttr<clang::UnusedAttr>() || d->hasAttr<clang::UsedAttr>() ||
         canon->hasAttr<clang::UnusedAttr>() || canon->hasAttr<clang::UsedAttr>();
}

bool named(const NamedDecl *d, Reporter &reporter) {
  if (d->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = d->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

class Collector : public clang::RecursiveASTVisitor<Collector> {
public:
  explicit Collector(const clang::SourceManager &sm) : sm(sm) {}

  bool VisitFunctionDecl(FunctionDecl *fn) {
    if (!fn || fn->isImplicit() || fn->isTemplateInstantiation() || !inMain(fn, sm)) {
      return true;
    }
    if (fn->getDeclContext()->isDependentContext()) {
      return true;
    }
    // A file that is only static functions has no external root. Flagging
    // every uncalled static there marks the whole program, including a
    // fixture whose static function is the test itself. A referenced static
    // does not open the gate. An external declaration does, including one
    // with no body in this file.
    if (fn->getFormalLinkage() != clang::Linkage::Internal) {
      hasExternal = true;
    }
    if (fn->isThisDeclarationADefinition() && fn->getFormalLinkage() == clang::Linkage::Internal) {
      const FunctionDecl *canon = fn->getCanonicalDecl();
      if (!functions.count(canon)) {
        functions.emplace(canon, fn);
      }
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (!vd || vd->isImplicit() || vd != vd->getCanonicalDecl() || !vd->isFileVarDecl() ||
        !inMain(vd, sm) || vd->getFormalLinkage() != clang::Linkage::Internal) {
      return true;
    }
    if (vd->getDeclContext()->isDependentContext() || !vd->isThisDeclarationADefinition()) {
      return true;
    }
    objects.push_back(vd);
    return true;
  }

  bool VisitTypedefNameDecl(TypedefNameDecl *td) {
    if (!td || td->isImplicit() || !inMain(td, sm) || td->getDeclContext()->isDependentContext()) {
      return true;
    }
    aliases.push_back(td);
    return true;
  }

  const clang::SourceManager &sm;
  bool hasExternal = false;
  std::map<const FunctionDecl *, const FunctionDecl *> functions;
  std::vector<const VarDecl *> objects;
  std::vector<const TypedefNameDecl *> aliases;
};

} // namespace

void NoUnusedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoUnusedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  const clang::SourceManager &sm = *result.SourceManager;
  Collector walk(sm);
  walk.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));

  for (const auto &entry : walk.functions) {
    const FunctionDecl *canon = entry.first;
    const FunctionDecl *fn = entry.second;
    if (!walk.hasExternal || canon->isReferenced() || fn->isReferenced() || canon->isUsed() ||
        fn->isUsed() || kept(fn) || named(fn, reporter)) {
      continue;
    }
    reporter.emit(sm, fn->getLocation(), kId, "this static function is never used");
  }
  for (const VarDecl *vd : walk.objects) {
    if (vd->isReferenced() || vd->isUsed() || kept(vd) || named(vd, reporter)) {
      continue;
    }
    reporter.emit(sm, vd->getLocation(), kId, "this file-scope object is never used");
  }
  for (const TypedefNameDecl *td : walk.aliases) {
    if (td->isReferenced() || td->isUsed() || kept(td) || named(td, reporter)) {
      continue;
    }
    reporter.emit(sm, td->getLocation(), kId, "this typedef is never used");
  }
}

#include "siliscope/AlignSame.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <optional>
#include <string>

using clang::VarDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.decl.align-same";

std::optional<unsigned> explicitAlign(const VarDecl *vd, clang::ASTContext &ctx) {
  bool any = false;
  unsigned bits = 0;
  for (const auto *attr : vd->specific_attrs<clang::AlignedAttr>()) {
    // A missing alignment is copied onto the definition as an inherited
    // attribute. That copy is not a second written alignment.
    if (attr->isInherited()) {
      continue;
    }
    any = true;
    const unsigned width = attr->getAlignment(ctx);
    if (width > bits) {
      bits = width;
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return bits;
}

bool allowed(const VarDecl *vd, Reporter &reporter) {
  if (vd->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = vd->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

void AlignSameCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl(unless(isImplicit())).bind("var"), this);
}

void AlignSameCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
  if (!vd || !result.Context || !result.SourceManager || vd != vd->getCanonicalDecl() ||
      !vd->isFileVarDecl()) {
    return;
  }
  if (allowed(vd, reporter)) {
    return;
  }
  bool saw = false;
  std::optional<unsigned> first;
  for (const VarDecl *redecl : vd->redecls()) {
    const std::optional<unsigned> align = explicitAlign(redecl, *result.Context);
    if (!saw) {
      first = align;
      saw = true;
      continue;
    }
    if (align != first) {
      reporter.emit(*result.SourceManager,
                    redecl->getLocation(),
                    kId,
                    "alignment differs across declarations");
      return;
    }
  }
}

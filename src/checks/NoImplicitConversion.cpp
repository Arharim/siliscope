#include "siliscope/NoImplicitConversion.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXConstructorDecl;
using clang::CXXConversionDecl;
using clang::ast_matchers::cxxConstructorDecl;
using clang::ast_matchers::cxxConversionDecl;

void NoImplicitConversionCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxConstructorDecl().bind("ctor"), this);
  finder.addMatcher(cxxConversionDecl().bind("conv"), this);
}

void NoImplicitConversionCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (const auto *conv = result.Nodes.getNodeAs<CXXConversionDecl>("conv")) {
    if (conv->isImplicit() || conv->isExplicit() || conv != conv->getCanonicalDecl() ||
        conv->isTemplateInstantiation()) {
      return;
    }
    reporter.emit(*result.SourceManager,
                  conv->getLocation(),
                  "ss.cpp.no-implicit-conversion",
                  "make this conversion explicit");
    return;
  }
  const auto *ctor = result.Nodes.getNodeAs<CXXConstructorDecl>("ctor");
  // isConvertingConstructor(false) is the non-explicit form. Copy and move
  // are not user-defined conversions. An inherited constructor is the base's.
  // The out-of-line definition repeats the in-class declaration. One report.
  if (!ctor || ctor != ctor->getCanonicalDecl() || ctor->isImplicit() ||
      ctor->isTemplateInstantiation() || ctor->isCopyOrMoveConstructor() ||
      ctor->isInheritingConstructor() || !ctor->isConvertingConstructor(/*AllowExplicit=*/false)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                ctor->getLocation(),
                "ss.cpp.no-implicit-conversion",
                "make this conversion explicit");
}

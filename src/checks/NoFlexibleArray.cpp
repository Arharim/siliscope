#include "siliscope/NoFlexibleArray.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::FieldDecl;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::hasType;
using clang::ast_matchers::incompleteArrayType;

void NoFlexibleArrayCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(fieldDecl(hasType(incompleteArrayType())).bind("fam"), this);
}

void NoFlexibleArrayCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *f = result.Nodes.getNodeAs<FieldDecl>("fam");
  if (!f) {
    return;
  }
  reporter.emit(*result.SourceManager,
                f->getLocation(),
                "ss.mem.no-flexible-array",
                "do not declare a flexible array member");
}

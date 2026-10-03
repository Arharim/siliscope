#include "siliscope/NoFriend.h"

#include "siliscope/RecordKind.h"
#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXRecordDecl;
using clang::FriendDecl;
using clang::ast_matchers::friendDecl;

void NoFriendCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(friendDecl().bind("friend"), this);
}

void NoFriendCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *decl = result.Nodes.getNodeAs<FriendDecl>("friend");
  if (!decl) {
    return;
  }
  if (const auto *rec = llvm::dyn_cast<CXXRecordDecl>(decl->getDeclContext())) {
    if (allowsPublicSurface(rec, *result.Context)) {
      return;
    }
  }
  const clang::SourceLocation loc =
      decl->getFriendLoc().isValid() ? decl->getFriendLoc() : decl->getLocation();
  reporter.emit(*result.SourceManager, loc, "ss.cpp.no-friend", "do not declare a friend");
}

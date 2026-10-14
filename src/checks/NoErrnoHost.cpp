#include "siliscope/NoErrnoHost.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::CallExpr;
using clang::DeclRefExpr;
using clang::FunctionDecl;
using clang::VarDecl;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::declRefExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;
using clang::ast_matchers::hasName;
using clang::ast_matchers::to;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.emb.no-errno-host";
constexpr const char *kMsg = "do not use host errno, locale, or file APIs";

bool projectDefinition(const FunctionDecl *fn) {
  for (const FunctionDecl *redecl : fn->redecls()) {
    if (redecl->isThisDeclarationADefinition() && !redecl->isImplicit()) {
      return true;
    }
  }
  return false;
}

bool allowed(const clang::Expr *expr, clang::ASTContext &ctx, Reporter &reporter) {
  if (!expr) {
    return false;
  }
  clang::DynTypedNode node = clang::DynTypedNode::create(*expr);
  for (int depth = 0; depth < 32; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      if (fn->getDeclName().isEmpty()) {
        return false;
      }
      const std::string name = fn->getDeclName().getAsString();
      return !name.empty() && reporter.allows(kId, name.c_str());
    }
  }
  return false;
}

} // namespace

void NoErrnoHostCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(declRefExpr(to(varDecl(hasName("errno")))).bind("errno"), this);
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("setlocale",
                                                            "localeconv",
                                                            "newlocale",
                                                            "uselocale",
                                                            "freelocale",
                                                            "duplocale",
                                                            "open",
                                                            "close",
                                                            "read",
                                                            "write",
                                                            "lseek",
                                                            "unlink",
                                                            "stat",
                                                            "fstat",
                                                            "lstat",
                                                            "opendir",
                                                            "readdir",
                                                            "closedir",
                                                            "fdopen",
                                                            "fileno",
                                                            "popen",
                                                            "pclose",
                                                            "mkdir",
                                                            "rmdir",
                                                            "creat",
                                                            "fcntl",
                                                            "ioctl",
                                                            "fopen",
                                                            "fclose",
                                                            "fread",
                                                            "fwrite",
                                                            "freopen",
                                                            "remove",
                                                            "rename",
                                                            "tmpfile",
                                                            "tmpnam"))))
                        .bind("call"),
                    this);
}

void NoErrnoHostCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context) {
    return;
  }
  if (const auto *ref = result.Nodes.getNodeAs<DeclRefExpr>("errno")) {
    const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl());
    if (!var || var->isLocalVarDeclOrParm() || allowed(ref, *result.Context, reporter)) {
      return;
    }
    reporter.emit(*result.SourceManager, ref->getLocation(), kId, kMsg);
    return;
  }
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  const FunctionDecl *fn = call ? call->getDirectCallee() : nullptr;
  if (!fn || llvm::isa<clang::CXXMethodDecl>(fn) || projectDefinition(fn)) {
    return;
  }
  if (fn->getFormalLinkage() == clang::Linkage::Internal ||
      allowed(call, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, call->getBeginLoc(), kId, kMsg);
}

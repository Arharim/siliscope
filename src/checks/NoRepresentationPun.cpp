#include "siliscope/NoRepresentationPun.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <map>
#include <set>
#include <string>

using clang::DeclRefExpr;
using clang::Expr;
using clang::FieldDecl;
using clang::FunctionDecl;
using clang::MemberExpr;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.type.no-representation-pun";
constexpr const char *kMsg = "do not pun this value through another union member";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

class FindPun : public clang::RecursiveASTVisitor<FindPun> {
public:
  explicit FindPun(Reporter &reporter, const clang::SourceManager &sm)
      : reporter(reporter), sm(sm) {}

  bool VisitMemberExpr(MemberExpr *expr) {
    if (!expr || !expr->getMemberDecl()) {
      return true;
    }
    const auto *field = llvm::dyn_cast<FieldDecl>(expr->getMemberDecl());
    if (!field || !field->getParent() || !field->getParent()->isUnion()) {
      return true;
    }
    const Expr *base = expr->getBase() ? expr->getBase()->IgnoreParenImpCasts() : nullptr;
    const auto *ref = base ? llvm::dyn_cast<DeclRefExpr>(base) : nullptr;
    const auto *var = ref ? llvm::dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    if (!var || !var->getType()->isUnionType()) {
      return true;
    }
    std::set<const FieldDecl *> &seen = fields[var->getCanonicalDecl()];
    const FieldDecl *canon = field->getCanonicalDecl();
    if (!seen.empty() && !seen.count(canon)) {
      reporter.emit(sm, expr->getExprLoc(), kId, kMsg);
    }
    seen.insert(canon);
    return true;
  }

private:
  Reporter &reporter;
  const clang::SourceManager &sm;
  std::map<const VarDecl *, std::set<const FieldDecl *>> fields;
};

} // namespace

void NoRepresentationPunCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoRepresentationPunCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || fn->isTemplateInstantiation() ||
      allowed(fn, reporter)) {
    return;
  }
  FindPun walk(reporter, *result.SourceManager);
  walk.TraverseStmt(fn->getBody());
}

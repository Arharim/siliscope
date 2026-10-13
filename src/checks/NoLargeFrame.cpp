#include "siliscope/NoLargeFrame.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <limits>
#include <string>

using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr uint64_t kFrameLimit = 512;

class FrameWalker : public clang::RecursiveASTVisitor<FrameWalker> {
public:
  explicit FrameWalker(clang::ASTContext &ctx) : ctx(ctx) {}

  bool TraverseFunctionDecl(FunctionDecl *) { return true; }
  bool TraverseCXXMethodDecl(clang::CXXMethodDecl *) { return true; }
  bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl *) { return true; }
  bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl *) { return true; }
  bool TraverseCXXConversionDecl(clang::CXXConversionDecl *) { return true; }
  bool TraverseLambdaExpr(clang::LambdaExpr *) { return true; }
  bool TraverseBlockExpr(clang::BlockExpr *) { return true; }

  bool VisitVarDecl(VarDecl *vd) {
    if (tripped || !vd || llvm::isa<ParmVarDecl>(vd)) {
      return true;
    }
    if (vd->isStaticLocal() || !vd->hasLocalStorage()) {
      return true;
    }
    const clang::QualType type = vd->getType();
    if (type.isNull() || type->isReferenceType() || type->isDependentType() ||
        type->isIncompleteType() || !type->isConstantSizeType()) {
      return true;
    }
    const clang::CharUnits bytes = ctx.getTypeSizeInChars(type);
    if (bytes.isNegative()) {
      return true;
    }
    const auto size = static_cast<uint64_t>(bytes.getQuantity());
    if (sum > std::numeric_limits<uint64_t>::max() - size) {
      sum = std::numeric_limits<uint64_t>::max();
    } else {
      sum += size;
    }
    if (sum > kFrameLimit) {
      tripped = vd;
    }
    return true;
  }

  const VarDecl *over() const { return tripped; }

private:
  clang::ASTContext &ctx;
  uint64_t sum = 0;
  const VarDecl *tripped = nullptr;
};

} // namespace

void NoLargeFrameCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoLargeFrameCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.Context || !result.SourceManager ||
      fn->isTemplateInstantiation()) {
    return;
  }
  if (!fn->getDeclName().isEmpty()) {
    const std::string name = fn->getDeclName().getAsString();
    if (!name.empty() && reporter.allows("ss.mem.no-large-frame", name.c_str())) {
      return;
    }
  }
  FrameWalker walker(*result.Context);
  walker.TraverseStmt(fn->getBody());
  const VarDecl *over = walker.over();
  if (!over) {
    return;
  }
  reporter.emit(*result.SourceManager,
                over->getLocation(),
                "ss.mem.no-large-frame",
                "this function's locals exceed 512 bytes");
}

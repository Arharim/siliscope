#include "siliscope/Distinct.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Linkage.h"
#include "llvm/ADT/StringRef.h"

#include <unordered_map>
#include <vector>

using clang::EnumConstantDecl;
using clang::FieldDecl;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::NamedDecl;
using clang::SwitchStmt;
using clang::TranslationUnitDecl;
using clang::TypedefNameDecl;
using clang::VarDecl;
using clang::WhileStmt;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr unsigned kExternalSig = 31;
constexpr unsigned kInternalSig = 63;

static bool samePrefix(llvm::StringRef a, llvm::StringRef b, unsigned n) {
  return a != b && a.take_front(n) == b.take_front(n);
}

static bool isExternalIdent(const NamedDecl *d) {
  if (!llvm::isa<VarDecl>(d) && !llvm::isa<FunctionDecl>(d)) {
    return false;
  }
  return d->getFormalLinkage() == clang::Linkage::External;
}

class DistinctVisitor : public clang::RecursiveASTVisitor<DistinctVisitor> {
public:
  DistinctVisitor(Reporter &r, const clang::SourceManager &sm) : reporter(r), sm(sm) {}

  bool TraverseTranslationUnitDecl(TranslationUnitDecl *d) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseTranslationUnitDecl(d);
    pop();
    return ok;
  }

  bool TraverseFunctionDecl(FunctionDecl *d) {
    if (!d || d->isImplicit()) {
      return true;
    }
    add(d);
    if (!d->isThisDeclarationADefinition() || !d->getBody()) {
      return true;
    }
    push();
    for (auto *p : d->parameters()) {
      if (!TraverseDecl(p)) {
        pop();
        return false;
      }
    }
    const bool ok = TraverseStmt(d->getBody());
    pop();
    return ok;
  }

  bool TraverseCompoundStmt(clang::CompoundStmt *s) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseCompoundStmt(s);
    pop();
    return ok;
  }

  bool TraverseForStmt(ForStmt *s) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseForStmt(s);
    pop();
    return ok;
  }

  bool TraverseIfStmt(IfStmt *s) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseIfStmt(s);
    pop();
    return ok;
  }

  bool TraverseSwitchStmt(SwitchStmt *s) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseSwitchStmt(s);
    pop();
    return ok;
  }

  bool TraverseWhileStmt(WhileStmt *s) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseWhileStmt(s);
    pop();
    return ok;
  }

  bool TraverseRecordDecl(clang::RecordDecl *d) {
    push();
    const bool ok = RecursiveASTVisitor::TraverseRecordDecl(d);
    pop();
    return ok;
  }

  bool VisitVarDecl(VarDecl *d) {
    if (d && !d->isLocalExternDecl()) {
      add(d);
    }
    return true;
  }

  bool VisitTypedefNameDecl(TypedefNameDecl *d) {
    add(d);
    return true;
  }

  bool VisitEnumConstantDecl(EnumConstantDecl *d) {
    add(d);
    return true;
  }

  bool VisitFieldDecl(FieldDecl *d) {
    add(d);
    return true;
  }

private:
  using Scope = std::unordered_map<const clang::IdentifierInfo *, const NamedDecl *>;

  void push() { scopes.emplace_back(); }

  void pop() {
    if (!scopes.empty()) {
      scopes.pop_back();
    }
  }

  void add(NamedDecl *d) {
    if (!d || !d->getIdentifier() || d->isImplicit() || scopes.empty()) {
      return;
    }
    const clang::IdentifierInfo *id = d->getIdentifier();
    Scope &cur = scopes.back();
    if (cur.find(id) != cur.end()) {
      return;
    }
    const llvm::StringRef n = d->getName();
    bool hit = false;
    for (const auto &kv : cur) {
      if (samePrefix(kv.first->getName(), n, kInternalSig)) {
        reporter.emit(sm,
                      d->getLocation(),
                      "ss.decl.distinct",
                      "identifier is not distinct in the significant characters");
        hit = true;
        break;
      }
    }
    cur[id] = d;
    if (isExternalIdent(d)) {
      if (!hit) {
        for (const NamedDecl *o : externals) {
          if (samePrefix(o->getName(), n, kExternalSig)) {
            reporter.emit(sm,
                          d->getLocation(),
                          "ss.decl.distinct",
                          "identifier is not distinct in the significant characters");
            break;
          }
        }
      }
      externals.push_back(d);
    }
  }

  Reporter &reporter;
  const clang::SourceManager &sm;
  std::vector<Scope> scopes;
  std::vector<const NamedDecl *> externals;
};

} // namespace

void DistinctCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void DistinctCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  DistinctVisitor v(reporter, *result.SourceManager);
  v.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
}

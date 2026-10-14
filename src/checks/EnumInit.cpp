#include "siliscope/EnumInit.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>
#include <vector>

using clang::EnumConstantDecl;
using clang::EnumDecl;
using clang::ast_matchers::enumDecl;

namespace {

constexpr const char *kId = "ss.type.enum-init";
constexpr const char *kMix = "initialize none, only the first, or every enumerator";
constexpr const char *kClash = "enumerator values collide";

bool allowed(const EnumDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

void EnumInitCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(enumDecl().bind("enum"), this);
}

void EnumInitCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *decl = result.Nodes.getNodeAs<EnumDecl>("enum");
  if (!decl || !decl->isComplete() || !result.SourceManager || allowed(decl, reporter)) {
    return;
  }
  std::vector<const EnumConstantDecl *> values;
  for (const EnumConstantDecl *item : decl->enumerators()) {
    values.push_back(item);
  }
  if (values.empty()) {
    return;
  }
  unsigned inits = 0;
  bool onlyFirst = true;
  for (unsigned i = 0; i < values.size(); ++i) {
    if (!values[i]->getInitExpr()) {
      continue;
    }
    ++inits;
    if (i != 0) {
      onlyFirst = false;
    }
  }
  const bool patterned = inits == 0 || inits == values.size() || (inits == 1 && onlyFirst);
  if (!patterned) {
    reporter.emit(*result.SourceManager, decl->getLocation(), kId, kMix);
  }
  for (unsigned i = 0; i < values.size(); ++i) {
    for (unsigned j = 0; j < i; ++j) {
      if (llvm::APSInt::compareValues(values[i]->getInitVal(), values[j]->getInitVal()) != 0) {
        continue;
      }
      reporter.emit(*result.SourceManager, values[i]->getLocation(), kId, kClash);
      break;
    }
  }
}

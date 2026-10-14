#include "siliscope/NoReserved.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringSet.h"

#include <string>

using clang::FunctionDecl;
using clang::NamedDecl;
using clang::VarDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.decl.no-reserved";
constexpr const char *kReserved = "do not use a reserved identifier";
constexpr const char *kLibc = "do not redefine a library name";

bool reserved(llvm::StringRef name) {
  return name.size() >= 2 && name[0] == '_' &&
         (name[1] == '_' || (name[1] >= 'A' && name[1] <= 'Z'));
}

bool libcName(llvm::StringRef name) {
  static const llvm::StringSet<> names = {
      "abort",   "abs",      "atof",    "atoi",     "atol",      "bsearch", "calloc",     "exit",
      "fclose",  "feof",     "ferror",  "fflush",   "fgetc",     "fgets",   "fopen",      "fprintf",
      "fputc",   "fputs",    "fread",   "free",     "freopen",   "fscanf",  "fseek",      "ftell",
      "fwrite",  "getc",     "getchar", "getenv",   "gets",      "isalnum", "isalpha",    "isdigit",
      "islower", "isprint",  "isspace", "isupper",  "isxdigit",  "labs",    "localeconv", "longjmp",
      "malloc",  "memchr",   "memcmp",  "memcpy",   "memmove",   "memset",  "perror",     "printf",
      "putc",    "putchar",  "puts",    "qsort",    "rand",      "realloc", "remove",     "rename",
      "rewind",  "scanf",    "setbuf",  "setjmp",   "setlocale", "signal",  "sprintf",    "sqrt",
      "srand",   "sscanf",   "strcat",  "strchr",   "strcmp",    "strcpy",  "strerror",   "strlen",
      "strncat", "strncmp",  "strncpy", "strrchr",  "strspn",    "strstr",  "strtod",     "strtok",
      "strtol",  "strtoul",  "system",  "time",     "tmpfile",   "tmpnam",  "tolower",    "toupper",
      "ungetc",  "vfprintf", "vprintf", "vsprintf", "errno",     "stderr",  "stdin",      "stdout"};
  return names.contains(name);
}

bool allowed(const NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool externalDefinition(const NamedDecl *decl) {
  if (const auto *fn = llvm::dyn_cast<FunctionDecl>(decl)) {
    if (!fn->isThisDeclarationADefinition() || fn->isImplicit() ||
        llvm::isa<clang::CXXMethodDecl>(fn) || !fn->getIdentifier()) {
      return false;
    }
    const llvm::StringRef name = fn->getName();
    if (name == "main" || name == "_init" || name == "_fini") {
      return false;
    }
    return fn->getFormalLinkage() != clang::Linkage::Internal;
  }
  const auto *var = llvm::dyn_cast<VarDecl>(decl);
  if (!var || !var->isThisDeclarationADefinition() || !var->isFileVarDecl() || var->isImplicit() ||
      !var->getIdentifier()) {
    return false;
  }
  return var->getFormalLinkage() != clang::Linkage::Internal;
}

class Visit : public clang::RecursiveASTVisitor<Visit> {
public:
  explicit Visit(const clang::SourceManager &sm, Reporter &reporter) : sm(sm), reporter(reporter) {}

  bool VisitNamedDecl(NamedDecl *decl) {
    if (!decl || decl->isImplicit() || decl->getDeclName().isEmpty() || !decl->getIdentifier() ||
        allowed(decl, reporter)) {
      return true;
    }
    if (const auto *fn = llvm::dyn_cast<FunctionDecl>(decl)) {
      if (fn->getBuiltinID() != 0) {
        return true;
      }
    }
    const llvm::StringRef name = decl->getName();
    if (reserved(name)) {
      reporter.emit(sm, decl->getLocation(), kId, kReserved);
      return true;
    }
    if (externalDefinition(decl) && libcName(name)) {
      reporter.emit(sm, decl->getLocation(), kId, kLibc);
    }
    return true;
  }

  const clang::SourceManager &sm;
  Reporter &reporter;
};

} // namespace

void NoReservedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoReservedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  Visit visit(*result.SourceManager, reporter);
  visit.TraverseDecl(const_cast<clang::TranslationUnitDecl *>(tu));
}

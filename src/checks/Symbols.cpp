#include "siliscope/Symbols.h"

#include "siliscope/FunctionKey.h"
#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/UnifiedSymbolResolution/USRGeneration.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using clang::FunctionDecl;
using clang::NamedDecl;
using clang::ParmVarDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::TranslationUnitDecl;
using clang::VarDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kOneDef = "ss.fn.single-definition";
constexpr const char *kHeader = "ss.fn.header-decl";
constexpr const char *kNames = "ss.fn.param-names-consistent";
constexpr const char *kExtern = "ss.decl.extern-in-one-header";
constexpr const char *kOneDefMsg = "this symbol is defined more than once";
constexpr const char *kHeaderMsg = "declare this in a header";
constexpr const char *kNamesMsg = "parameter name does not match the declaration";
constexpr const char *kExternMsg = "declare this object in one header";

struct ParamUse {
  std::string name;
  DiagSite at;
};

struct Signature {
  std::vector<ParamUse> params;
  bool definition = false;
  std::string allow;
};

// One external definition in one translation unit. A tentative definition
// yields to a later initializer in the same file.
struct DefSite {
  std::string tu;
  DiagSite at;
  bool real = false;
  std::string allow;
};

// A C definition written in the main file with no declaration outside it.
struct Gap {
  std::string tu;
  DiagSite at;
  bool real = false;
  std::string allow;
};

struct ExternSite {
  std::string file;
  bool source = false;
  DiagSite at;
  std::string allow;
};

struct Symbol {
  std::vector<DefSite> defs;
  std::vector<Gap> gaps;
  std::vector<Signature> sigs;
  std::vector<ExternSite> externs;
};

std::string allowName(const NamedDecl *d) {
  if (!d) {
    return {};
  }
  const clang::DeclarationName dn = d->getDeclName();
  if (dn.isEmpty()) {
    return {};
  }
  return dn.getAsString();
}

std::string varKey(const VarDecl *vd) {
  if (!vd) {
    return {};
  }
  vd = vd->getCanonicalDecl();
  llvm::SmallString<256> buf;
  if (clang::index::generateUSRForDecl(vd, buf) || buf.empty()) {
    return {};
  }
  return std::string(buf);
}

std::string mainPath(const SourceManager &sm) {
  const SourceLocation start = sm.getLocForStartOfFile(sm.getMainFileID());
  const clang::PresumedLoc pl = sm.getPresumedLoc(start);
  if (pl.isInvalid() || !pl.getFilename()) {
    return {};
  }
  return pl.getFilename();
}

bool spelledInMain(const SourceManager &sm, SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  return loc.isValid() && sm.getFileID(loc) == sm.getMainFileID();
}

// Same test static-internal uses: a declaration whose expansion is not the
// main file is the visible header (or another included file).
bool declaredOutsideMain(const NamedDecl *d, const SourceManager &sm) {
  for (const clang::Decl *r : d->redecls()) {
    SourceLocation loc = r->getLocation();
    if (loc.isInvalid()) {
      continue;
    }
    loc = sm.getExpansionLoc(loc);
    if (loc.isValid() && !sm.isWrittenInMainFile(loc)) {
      return true;
    }
  }
  return false;
}

bool looksLikeHeader(llvm::StringRef path) {
  return path.ends_with(".h") || path.ends_with(".hh") || path.ends_with(".hpp") ||
         path.ends_with(".hxx") || path.ends_with(".h++");
}

bool isRuntimeEntry(const FunctionDecl *fn) {
  if (!fn->getIdentifier()) {
    return false;
  }
  const llvm::StringRef name = fn->getName();
  return fn->isMain() || name == "main" || name == "_init" || name == "_fini" || isIsr(fn);
}

// A template, an implicit instantiation, and a dependent pattern may be
// textually repeated. An explicit specialization is one definition.
bool templateMayRepeat(const FunctionDecl *fn) {
  if (fn->getDescribedFunctionTemplate() || fn->isDependentContext()) {
    return true;
  }
  return fn->isTemplateInstantiation();
}

bool templateMayRepeat(const VarDecl *vd) {
  if (vd->getDescribedVarTemplate() || vd->getDeclContext()->isDependentContext()) {
    return true;
  }
  const clang::TemplateSpecializationKind kind = vd->getTemplateSpecializationKind();
  return kind == clang::TSK_ImplicitInstantiation ||
         kind == clang::TSK_ExplicitInstantiationDeclaration ||
         kind == clang::TSK_ExplicitInstantiationDefinition;
}

// C++ inline (including constexpr and an in-class body) may be copied into
// every translation unit. In C, `inline` without `extern` is not an external
// definition; `extern inline` is.
bool inlineMayRepeat(const FunctionDecl *fn, bool cpp) {
  if (templateMayRepeat(fn)) {
    return true;
  }
  if (cpp) {
    return fn->isInlined();
  }
  if (!fn->isInlineSpecified()) {
    return false;
  }
  for (const FunctionDecl *r : fn->redecls()) {
    if (!r || r->getStorageClass() == clang::SC_Extern || !r->isInlineSpecified()) {
      return false;
    }
  }
  return true;
}

void addDef(Symbol &sym, DefSite site) {
  for (DefSite &have : sym.defs) {
    if (have.tu != site.tu) {
      continue;
    }
    if (site.real && !have.real) {
      have = std::move(site);
    }
    return;
  }
  sym.defs.push_back(std::move(site));
}

void addGap(Symbol &sym, Gap site) {
  for (Gap &have : sym.gaps) {
    if (have.tu != site.tu) {
      continue;
    }
    if (site.real && !have.real) {
      have = std::move(site);
    }
    return;
  }
  sym.gaps.push_back(std::move(site));
}

void emitAt(
    Reporter &out, const DiagSite &site, const char *id, const char *msg, const std::string &name) {
  if (!name.empty() && out.allows(id, name.c_str())) {
    return;
  }
  out.emitSite(site, id, msg);
}

class Collector : public clang::RecursiveASTVisitor<Collector> {
public:
  Collector(std::map<std::string, Symbol> &symbols,
            const SourceManager &sm,
            Reporter &reporter,
            bool cpp)
      : symbols(symbols), sm(sm), reporter(reporter), cpp(cpp), tu(mainPath(sm)) {}

  bool TraverseStmt(clang::Stmt *, DataRecursionQueue * = nullptr) { return true; }

  bool VisitFunctionDecl(FunctionDecl *fn) {
    if (!fn || fn->isImplicit() || fn->getBuiltinID() != 0 || tu.empty()) {
      return true;
    }
    const std::string key = functionKey(fn, sm);
    if (key.empty()) {
      return true;
    }
    Symbol &sym = symbols[key];
    noteSignature(sym, fn);
    if (!fn->isThisDeclarationADefinition() || !fn->hasExternalFormalLinkage()) {
      return true;
    }
    const std::optional<DiagSite> at = reporter.locate(sm, fn->getLocation());
    if (!at) {
      return true;
    }
    const std::string allow = allowName(fn);
    if (!inlineMayRepeat(fn, cpp)) {
      DefSite def;
      def.tu = tu;
      def.at = *at;
      def.real = true;
      def.allow = allow;
      addDef(sym, std::move(def));
    }
    // main, reset hooks, and vector entries are not a module API.
    if (!cpp && spelledInMain(sm, fn->getLocation()) && !declaredOutsideMain(fn, sm) &&
        !isRuntimeEntry(fn)) {
      Gap gap;
      gap.tu = tu;
      gap.at = *at;
      gap.real = true;
      gap.allow = allow;
      addGap(sym, std::move(gap));
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *vd) {
    if (!vd || vd->isImplicit() || !vd->isFileVarDecl() || tu.empty()) {
      return true;
    }
    if (!vd->hasExternalFormalLinkage()) {
      return true;
    }
    const VarDecl::DefinitionKind kind = vd->isThisDeclarationADefinition();
    if (kind == VarDecl::DeclarationOnly) {
      noteExtern(vd);
      return true;
    }
    if (templateMayRepeat(vd) || vd->isInline()) {
      return true;
    }
    const std::optional<DiagSite> at = reporter.locate(sm, vd->getLocation());
    if (!at) {
      return true;
    }
    const std::string key = varKey(vd);
    if (key.empty()) {
      return true;
    }
    Symbol &sym = symbols[key];
    const std::string allow = allowName(vd);
    const bool real = kind == VarDecl::Definition;
    DefSite def;
    def.tu = tu;
    def.at = *at;
    def.real = real;
    def.allow = allow;
    addDef(sym, std::move(def));
    if (!cpp && spelledInMain(sm, vd->getLocation()) && !declaredOutsideMain(vd, sm)) {
      Gap gap;
      gap.tu = tu;
      gap.at = *at;
      gap.real = real;
      gap.allow = allow;
      addGap(sym, std::move(gap));
    }
    return true;
  }

private:
  void noteSignature(Symbol &sym, const FunctionDecl *fn) {
    Signature sig;
    sig.definition = fn->isThisDeclarationADefinition();
    sig.allow = allowName(fn);
    sig.params.reserve(fn->getNumParams());
    for (unsigned i = 0; i < fn->getNumParams(); ++i) {
      const ParmVarDecl *p = fn->getParamDecl(i);
      ParamUse use;
      if (p && p->getIdentifier()) {
        use.name = p->getIdentifier()->getName().str();
      }
      const SourceLocation loc =
          p && p->getLocation().isValid() ? p->getLocation() : fn->getLocation();
      if (const std::optional<DiagSite> at = reporter.locate(sm, loc)) {
        use.at = *at;
      }
      sig.params.push_back(std::move(use));
    }
    sym.sigs.push_back(std::move(sig));
  }

  void noteExtern(const VarDecl *vd) {
    if (cpp || vd->getStorageClass() != clang::SC_Extern) {
      return;
    }
    const SourceLocation spell = sm.getSpellingLoc(vd->getLocation());
    if (spell.isInvalid()) {
      return;
    }
    const clang::PresumedLoc pl = sm.getPresumedLoc(spell);
    if (pl.isInvalid() || !pl.getFilename()) {
      return;
    }
    const std::optional<DiagSite> at = reporter.locate(sm, vd->getLocation());
    if (!at) {
      return;
    }
    const std::string key = varKey(vd);
    if (key.empty()) {
      return;
    }
    const std::string file = pl.getFilename();
    const bool header = looksLikeHeader(file);
    const bool inMain = sm.getFileID(spell) == sm.getMainFileID();
    ExternSite site;
    site.file = file;
    site.source = inMain && !header;
    site.at = *at;
    site.allow = allowName(vd);
    symbols[key].externs.push_back(std::move(site));
  }

  std::map<std::string, Symbol> &symbols;
  const SourceManager &sm;
  Reporter &reporter;
  bool cpp = false;
  std::string tu;
};

void reportDefs(const Symbol &sym, Reporter &out) {
  if (sym.defs.size() < 2) {
    return;
  }
  for (const DefSite &def : sym.defs) {
    emitAt(out, def.at, kOneDef, kOneDefMsg, def.allow);
  }
}

void reportGaps(const Symbol &sym, Reporter &out) {
  for (const Gap &gap : sym.gaps) {
    emitAt(out, gap.at, kHeader, kHeaderMsg, gap.allow);
  }
}

void reportNames(const Symbol &sym, Reporter &out) {
  for (const Signature &def : sym.sigs) {
    if (!def.definition) {
      continue;
    }
    for (const Signature &other : sym.sigs) {
      if (&other == &def) {
        continue;
      }
      const unsigned n = static_cast<unsigned>(std::min(def.params.size(), other.params.size()));
      for (unsigned i = 0; i < n; ++i) {
        if (other.params[i].name.empty() || other.params[i].name == def.params[i].name) {
          continue;
        }
        emitAt(out, def.params[i].at, kNames, kNamesMsg, def.allow);
      }
    }
  }
}

void reportExterns(const Symbol &sym, Reporter &out) {
  std::set<std::string> headers;
  for (const ExternSite &site : sym.externs) {
    if (!site.source) {
      headers.insert(site.file);
    }
  }
  const bool split = headers.size() > 1;
  for (const ExternSite &site : sym.externs) {
    if (!site.source && !split) {
      continue;
    }
    emitAt(out, site.at, kExtern, kExternMsg, site.allow);
  }
}

class Pass : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  Pass(ProgramSymbols &symbols, Reporter &reporter) : symbols(symbols), reporter(reporter) {}

  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override {
    const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu || !result.SourceManager || !result.Context) {
      return;
    }
    symbols.record(const_cast<TranslationUnitDecl *>(tu), *result.SourceManager, reporter);
  }

private:
  ProgramSymbols &symbols;
  Reporter &reporter;
};

} // namespace

struct ProgramSymbols::Data {
  std::map<std::string, Symbol> symbols;
};

ProgramSymbols::ProgramSymbols() : data(std::make_unique<Data>()) {}

ProgramSymbols::~ProgramSymbols() = default;

void ProgramSymbols::record(TranslationUnitDecl *tu, const SourceManager &sm, Reporter &reporter) {
  if (!tu) {
    return;
  }
  const bool cpp = tu->getASTContext().getLangOpts().CPlusPlus;
  Collector collector(data->symbols, sm, reporter, cpp);
  collector.TraverseDecl(tu);
}

void ProgramSymbols::finish(Reporter &reporter) const {
  for (const auto &item : data->symbols) {
    reportDefs(item.second, reporter);
    reportGaps(item.second, reporter);
    reportNames(item.second, reporter);
    reportExterns(item.second, reporter);
  }
}

void SymbolCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void SymbolCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

void attachSymbols(clang::ast_matchers::MatchFinder &finder,
                   ProgramSymbols &symbols,
                   Reporter &reporter,
                   std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot) {
  slot = std::make_unique<Pass>(symbols, reporter);
  finder.addMatcher(translationUnitDecl().bind("tu"), slot.get());
}

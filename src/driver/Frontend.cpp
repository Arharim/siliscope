#include "siliscope/Frontend.h"

#include "siliscope/Braces.h"
#include "siliscope/Check.h"
#include "siliscope/NoAbortSystem.h"
#include "siliscope/NoAssignInCond.h"
#include "siliscope/NoAtoi.h"
#include "siliscope/NoComma.h"
#include "siliscope/NoGoto.h"
#include "siliscope/NoHeap.h"
#include "siliscope/NoOctal.h"
#include "siliscope/NoQsort.h"
#include "siliscope/NoRand.h"
#include "siliscope/NoSetjmp.h"
#include "siliscope/NoSetlocale.h"
#include "siliscope/NoSignal.h"
#include "siliscope/NoStdarg.h"
#include "siliscope/NoStdio.h"
#include "siliscope/NoUnboundedString.h"
#include "siliscope/NoVLA.h"
#include "siliscope/Profile.h"
#include "siliscope/Report.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

using clang::ASTConsumer;
using clang::ASTContext;
using clang::ASTFrontendAction;
using clang::CompilerInstance;
using clang::FrontendAction;
using clang::FunctionDecl;
using clang::PackedAttr;
using clang::RecordDecl;
using clang::ast_matchers::MatchFinder;
using clang::tooling::ClangTool;
using clang::tooling::CompilationDatabase;
using clang::tooling::FixedCompilationDatabase;
using clang::tooling::FrontendActionFactory;
using clang::tooling::JSONCompilationDatabase;

namespace {

struct Probe {
  unsigned functions = 0;
  unsigned interrupt = 0;
  unsigned packed = 0;
};

class ProbeVisitor : public clang::RecursiveASTVisitor<ProbeVisitor> {
public:
  explicit ProbeVisitor(Probe &p) : p(p) {}

  bool VisitFunctionDecl(FunctionDecl *d) {
    if (!d || !d->isThisDeclarationADefinition() || d->isImplicit()) {
      return true;
    }
    ++p.functions;
    if (d->hasAttrs()) {
      for (const auto *a : d->attrs()) {
        if (!a) {
          continue;
        }
        llvm::StringRef sp = a->getSpelling();
        if (sp.contains_insensitive("interrupt")) {
          ++p.interrupt;
        }
      }
    }
    return true;
  }

  bool VisitRecordDecl(RecordDecl *d) {
    if (!d || !d->isCompleteDefinition() || d->isImplicit()) {
      return true;
    }
    if (d->hasAttr<PackedAttr>()) {
      ++p.packed;
    }
    return true;
  }

private:
  Probe &p;
};

class ProbeConsumer : public ASTConsumer {
public:
  explicit ProbeConsumer(Probe &p) : p(p) {}

  void HandleTranslationUnit(ASTContext &ctx) override {
    ProbeVisitor v(p);
    v.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  Probe &p;
};

class AnalyzeAction : public ASTFrontendAction {
public:
  AnalyzeAction(Reporter &reporter, Probe *probe, const Profile &profile)
      : probe(probe),
        no_goto(reporter),
        no_setjmp(reporter),
        no_heap(reporter),
        no_unbounded(reporter),
        no_stdio(reporter),
        braces(reporter),
        no_assign(reporter),
        no_octal(reporter),
        no_vla(reporter),
        no_stdarg(reporter),
        no_signal(reporter),
        no_atoi(reporter),
        no_abort(reporter),
        no_qsort(reporter),
        no_rand(reporter),
        no_setlocale(reporter),
        no_comma(reporter) {
    Check *const all[] = {&no_goto,
                          &no_setjmp,
                          &no_heap,
                          &no_unbounded,
                          &no_stdio,
                          &braces,
                          &no_assign,
                          &no_octal,
                          &no_vla,
                          &no_stdarg,
                          &no_signal,
                          &no_atoi,
                          &no_abort,
                          &no_qsort,
                          &no_rand,
                          &no_setlocale,
                          &no_comma};
    const char *const ids[] = {"ss.ctrl.no-goto",
                               "ss.ctrl.no-setjmp",
                               "ss.mem.no-heap-after-init",
                               "ss.libc.no-unbounded-string",
                               "ss.libc.no-stdio",
                               "ss.ctrl.braces",
                               "ss.ctrl.no-assignment-in-condition",
                               "ss.expr.no-octal",
                               "ss.mem.no-vla",
                               "ss.fn.no-stdarg",
                               "ss.libc.no-signal",
                               "ss.libc.no-atoi",
                               "ss.libc.no-abort-system",
                               "ss.libc.no-qsort-bsearch",
                               "ss.libc.no-rand",
                               "ss.libc.no-setlocale",
                               "ss.expr.no-comma"};
    static_assert(sizeof(all) / sizeof(all[0]) == sizeof(ids) / sizeof(ids[0]));
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
      if (profile.isEnabled(ids[i])) {
        all[i]->registerMatchers(finder);
      }
    }
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
    std::vector<std::unique_ptr<ASTConsumer>> cs;
    cs.push_back(finder.newASTConsumer());
    if (probe) {
      cs.push_back(std::make_unique<ProbeConsumer>(*probe));
    }
    return std::make_unique<clang::MultiplexConsumer>(std::move(cs));
  }

private:
  Probe *probe;
  MatchFinder finder;
  NoGotoCheck no_goto;
  NoSetjmpCheck no_setjmp;
  NoHeapCheck no_heap;
  NoUnboundedStringCheck no_unbounded;
  NoStdioCheck no_stdio;
  BracesCheck braces;
  NoAssignInCondCheck no_assign;
  NoOctalCheck no_octal;
  NoVLACheck no_vla;
  NoStdargCheck no_stdarg;
  NoSignalCheck no_signal;
  NoAtoiCheck no_atoi;
  NoAbortSystemCheck no_abort;
  NoQsortCheck no_qsort;
  NoRandCheck no_rand;
  NoSetlocaleCheck no_setlocale;
  NoCommaCheck no_comma;
};

class AnalyzeFactory : public FrontendActionFactory {
public:
  AnalyzeFactory(Reporter &reporter, Probe *probe, const Profile &profile)
      : reporter(reporter), probe(probe), profile(profile) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<AnalyzeAction>(reporter, probe, profile);
  }

private:
  Reporter &reporter;
  Probe *probe;
  const Profile &profile;
};

std::unique_ptr<CompilationDatabase> loadCompilations(const FrontendOptions &opt,
                                                      std::string &err) {
  if (!opt.compile_commands_dir.empty()) {
    auto db = JSONCompilationDatabase::loadFromDirectory(opt.compile_commands_dir, err);
    if (db) {
      return db;
    }
    err = "compile_commands.json: " + err;
    return nullptr;
  }

  std::vector<std::string> cmd = {
      "-fsyntax-only",
      "-fgnuc-version=12.0.0",
      "--target=" + opt.target,
  };
  cmd.insert(cmd.end(), opt.extra_args.begin(), opt.extra_args.end());
  return std::make_unique<FixedCompilationDatabase>(".", cmd);
}

} // namespace

int runFrontend(const FrontendOptions &opt) {
  std::string err;
  Profile profile;
  if (!loadProfile(opt.ruleset_dir, opt.profile, profile, err)) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }

  auto db = loadCompilations(opt, err);
  if (!db) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }

  Probe probe;
  Reporter reporter(profile);
  ClangTool tool(*db, opt.sources);
  AnalyzeFactory factory(reporter, opt.probe ? &probe : nullptr, profile);
  const int parse_rc = tool.run(&factory);

  if (opt.probe) {
    llvm::outs() << "target: " << opt.target << "\n"
                 << "files: " << opt.sources.size() << "\n"
                 << "functions: " << probe.functions << "\n"
                 << "interrupt: " << probe.interrupt << "\n"
                 << "packed: " << probe.packed << "\n";
  }
  reporter.printSummary();

  if (parse_rc != 0) {
    return parse_rc;
  }
  return reporter.count() ? 1 : 0;
}

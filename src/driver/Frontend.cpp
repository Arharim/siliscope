#include "siliscope/Frontend.h"

#include "siliscope/Check.h"
#include "siliscope/Profile.h"
#include "siliscope/Registry.h"
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
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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
using clang::tooling::CommandLineArguments;
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
  AnalyzeAction(Reporter &reporter, Probe *probe, const Profile &profile) : probe(probe) {
    const CheckSpec *specs = checkSpecs();
    for (unsigned i = 0; i < checkSpecCount(); ++i) {
      if (!profile.isEnabled(specs[i].id)) {
        continue;
      }
      checks.push_back(specs[i].make(reporter));
      checks.back()->registerMatchers(finder);
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
  std::vector<std::unique_ptr<Check>> checks;
  MatchFinder finder;
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

// GNU arm-none-eabi-gcc lives in <prefix>/bin; headers are <prefix>/arm-none-eabi.
static llvm::SmallString<256> gnuArmPrefix(llvm::StringRef compiler) {
  llvm::SmallString<256> path(compiler);
  llvm::sys::path::remove_filename(path);
  llvm::sys::path::remove_filename(path);
  return path;
}

static std::string gnuArmSysroot(llvm::StringRef compiler) {
  llvm::SmallString<256> path = gnuArmPrefix(compiler);
  llvm::sys::path::append(path, "arm-none-eabi");
  if (llvm::sys::fs::is_directory(path)) {
    return std::string(path);
  }
  return {};
}

// lib/gcc/arm-none-eabi/<ver>/include holds stddef.h / stdarg.h for the cross gcc.
static std::string gnuArmGccInclude(llvm::StringRef compiler) {
  llvm::SmallString<256> dir = gnuArmPrefix(compiler);
  llvm::sys::path::append(dir, "lib", "gcc", "arm-none-eabi");
  std::error_code ec;
  for (llvm::sys::fs::directory_iterator it(dir, ec), e; it != e && !ec; it.increment(ec)) {
    llvm::SmallString<256> inc(it->path());
    llvm::sys::path::append(inc, "include");
    if (llvm::sys::fs::is_directory(inc)) {
      return std::string(inc);
    }
  }
  return {};
}

static unsigned countMissingCheckers(const Profile &profile) {
  unsigned missing = 0;
  for (const auto &kv : profile.enabled) {
    if (!hasChecker(kv.first.c_str())) {
      ++missing;
    }
  }
  return missing;
}

static int printRuleList(const Profile &profile) {
  std::vector<std::string> ids;
  ids.reserve(profile.enabled.size());
  for (const auto &kv : profile.enabled) {
    ids.push_back(kv.first);
  }
  std::sort(ids.begin(), ids.end());

  unsigned with = 0;
  for (const std::string &id : ids) {
    if (hasChecker(id.c_str())) {
      ++with;
    }
  }

  llvm::outs() << "profile: " << profile.name << "\n";
  llvm::outs() << "languages:";
  for (const std::string &lang : profile.languages) {
    llvm::outs() << " " << lang;
  }
  llvm::outs() << "\n";
  llvm::outs() << "enabled: " << ids.size() << "\n";
  llvm::outs() << "with-checker: " << with << "\n";
  llvm::outs() << "no-checker: " << (ids.size() - with) << "\n";
  for (const std::string &id : ids) {
    const char *severity = profile.severityOf(id.c_str());
    const char *kind = profile.kindOf(id.c_str());
    llvm::outs() << id << "\t" << (severity ? severity : "") << "\t" << (kind ? kind : "") << "\t"
                 << (hasChecker(id.c_str()) ? "checker" : "no-checker") << "\n";
  }
  return 0;
}

} // namespace

int runFrontend(const FrontendOptions &opt) {
  std::string err;
  Profile profile;
  if (!loadProfile(opt.ruleset_dir, opt.profile, profile, err)) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }
  for (const auto &a : opt.allow) {
    profile.addAllow(a.first, a.second);
  }

  if (opt.list) {
    return printRuleList(profile);
  }

  auto db = loadCompilations(opt, err);
  if (!db) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }

  Probe probe;
  Reporter reporter(profile);
  ClangTool tool(*db, opt.sources);
  if (!opt.compile_commands_dir.empty()) {
    const std::string tgt = "--target=" + opt.target;
    const std::vector<std::string> extras = opt.extra_args;
    tool.appendArgumentsAdjuster([tgt, extras](const CommandLineArguments &args, llvm::StringRef) {
      CommandLineArguments out;
      if (!args.empty()) {
        out.push_back(args.front());
        const llvm::StringRef compiler = args.front();
        if (compiler.contains_insensitive("arm-none-eabi-gcc") ||
            compiler.contains_insensitive("arm-none-eabi-g++")) {
          out.push_back(tgt);
          const std::string sys = gnuArmSysroot(compiler);
          if (!sys.empty()) {
            out.push_back("--sysroot=" + sys);
          }
          const std::string gccinc = gnuArmGccInclude(compiler);
          if (!gccinc.empty()) {
            out.push_back("-isystem");
            out.push_back(gccinc);
          }
        }
      }
      for (size_t i = 1; i < args.size(); ++i) {
        if (llvm::StringRef(args[i]).starts_with("--specs=")) {
          continue;
        }
        out.push_back(args[i]);
      }
      out.insert(out.end(), extras.begin(), extras.end());
      return out;
    });
  }
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
  llvm::outs() << "no-checker: " << countMissingCheckers(profile) << "\n";

  if (parse_rc != 0) {
    return parse_rc;
  }
  return reporter.count() ? 1 : 0;
}

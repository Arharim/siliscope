#include "siliscope/PreprocessorCheck.h"
#include "siliscope/Report.h"

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <utility>
#include <vector>

using clang::FileID;
using clang::IdentifierInfo;
using clang::MacroDirective;
using clang::MacroInfo;
using clang::Preprocessor;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Token;

namespace {

bool absoluteInclude(llvm::StringRef name) {
  name = name.trim();
  if (name.empty()) {
    return false;
  }
  const char front = name.front();
  if (front == '/' || front == '\\') {
    return true;
  }
  const auto alpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
  return name.size() >= 2 && alpha(name[0]) && name[1] == ':';
}

bool isParameter(const MacroInfo *mi, const Token &tok) {
  if (!mi || !tok.isAnyIdentifier()) {
    return false;
  }
  const IdentifierInfo *ii = tok.getIdentifierInfo();
  return ii && mi->getParameterNum(ii) >= 0;
}

// Clang keeps one conditional stack per file. A #endif in another file is
// diagnosed as "endif without if" and never reaches PPCallbacks::Endif, so
// the still-open directive is reported when its file is left.
class Pass : public clang::PPCallbacks {
public:
  Pass(const Preprocessor &pp, Reporter &reporter) : pp(pp), reporter(reporter) {}

  void InclusionDirective(SourceLocation HashLoc,
                          const Token &,
                          llvm::StringRef FileName,
                          bool,
                          clang::CharSourceRange FilenameRange,
                          clang::OptionalFileEntryRef,
                          llvm::StringRef,
                          llvm::StringRef,
                          const clang::Module *,
                          bool,
                          clang::SrcMgr::CharacteristicKind) override {
    if (!absoluteInclude(FileName)) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    SourceLocation loc = FilenameRange.getBegin();
    if (loc.isInvalid()) {
      loc = HashLoc;
    }
    reporter.emit(sm, loc, "ss.pre.no-path-in-include", "an include name is an absolute path");
  }

  void If(SourceLocation Loc, clang::SourceRange, ConditionValueKind) override { noteOpen(Loc); }

  void Ifdef(SourceLocation Loc, const Token &, const clang::MacroDefinition &) override {
    noteOpen(Loc);
  }

  void Ifndef(SourceLocation Loc, const Token &, const clang::MacroDefinition &) override {
    noteOpen(Loc);
  }

  void Endif(SourceLocation Loc, SourceLocation IfLoc) override {
    if (Loc.isValid() && IfLoc.isValid()) {
      const SourceManager &sm = pp.getSourceManager();
      const SourceLocation end = sm.getSpellingLoc(Loc);
      const SourceLocation open = sm.getSpellingLoc(IfLoc);
      if (end.isValid() && open.isValid() && sm.getFileID(end) != sm.getFileID(open)) {
        SourceLocation at = end;
        if (sm.isInSystemHeader(end) && !sm.isInSystemHeader(open)) {
          at = open;
        }
        reportUnclosed(at);
      }
    }
    noteClose(Loc);
  }

  void LexedFileChanged(FileID,
                        LexedFileChangeReason Reason,
                        clang::SrcMgr::CharacteristicKind,
                        FileID PrevFID,
                        SourceLocation) override {
    if (Reason == LexedFileChangeReason::ExitFile) {
      closeFile(PrevFID);
    }
  }

  void EndOfMainFile() override {
    const std::vector<OpenCond> leftover = std::move(opens);
    opens.clear();
    for (const OpenCond &open : leftover) {
      reportUnclosed(open.loc);
    }
  }

  void MacroDefined(const Token &MacroNameTok, const MacroDirective *MD) override {
    const SourceManager &sm = pp.getSourceManager();
    if (!MacroNameTok.isAnnotation() && !MacroNameTok.isLiteral() &&
        !MacroNameTok.is(clang::tok::raw_identifier) && !MacroNameTok.is(clang::tok::eof)) {
      if (const IdentifierInfo *ii = MacroNameTok.getIdentifierInfo()) {
        if (ii->isKeyword(pp.getLangOpts()) || ii->isCPlusPlusOperatorKeyword()) {
          reporter.emit(sm,
                        MacroNameTok.getLocation(),
                        "ss.pre.no-keyword-macro",
                        "do not redefine a keyword");
        }
      }
    }
    const MacroInfo *mi = MD ? MD->getMacroInfo() : nullptr;
    if (!mi || !mi->isFunctionLike()) {
      return;
    }
    const unsigned n = mi->getNumTokens();
    for (unsigned i = 0; i + 2 < n; ++i) {
      const Token &a = mi->getReplacementToken(i);
      const Token &b = mi->getReplacementToken(i + 1);
      const Token &c = mi->getReplacementToken(i + 2);
      // # param ##   and   ## # param
      if (a.is(clang::tok::hash) && isParameter(mi, b) && c.is(clang::tok::hashhash)) {
        reporter.emit(sm,
                      a.getLocation(),
                      "ss.pre.no-stringify-then-paste",
                      "do not stringify a macro parameter and paste it");
      }
      if (a.is(clang::tok::hashhash) && b.is(clang::tok::hash) && isParameter(mi, c)) {
        reporter.emit(sm,
                      b.getLocation(),
                      "ss.pre.no-stringify-then-paste",
                      "do not stringify a macro parameter and paste it");
      }
    }
  }

private:
  struct OpenCond {
    SourceLocation loc;
    FileID file;
  };

  void noteOpen(SourceLocation loc) {
    if (loc.isInvalid()) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    const SourceLocation spell = sm.getSpellingLoc(loc);
    if (spell.isInvalid()) {
      return;
    }
    opens.push_back({spell, sm.getFileID(spell)});
  }

  void noteClose(SourceLocation endifLoc) {
    if (opens.empty() || endifLoc.isInvalid()) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    const SourceLocation spell = sm.getSpellingLoc(endifLoc);
    if (spell.isInvalid()) {
      return;
    }
    const FileID file = sm.getFileID(spell);
    for (unsigned i = static_cast<unsigned>(opens.size()); i > 0; --i) {
      if (opens[i - 1].file == file) {
        opens.erase(opens.begin() + static_cast<std::ptrdiff_t>(i - 1));
        return;
      }
    }
  }

  void closeFile(FileID file) {
    if (file.isInvalid() || opens.empty()) {
      return;
    }
    std::vector<OpenCond> kept;
    kept.reserve(opens.size());
    for (const OpenCond &open : opens) {
      if (open.file == file) {
        reportUnclosed(open.loc);
      } else {
        kept.push_back(open);
      }
    }
    opens.swap(kept);
  }

  void reportUnclosed(SourceLocation loc) {
    reporter.emit(pp.getSourceManager(),
                  loc,
                  "ss.pre.ifdef-same-file",
                  "close this conditional in the file that opened it");
  }

  const Preprocessor &pp;
  Reporter &reporter;
  std::vector<OpenCond> opens;
};

} // namespace

void PreprocessorCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void PreprocessorCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

void attachPreprocessorPass(Preprocessor &pp, Reporter &reporter) {
  pp.addPPCallbacks(std::make_unique<Pass>(pp, reporter));
}

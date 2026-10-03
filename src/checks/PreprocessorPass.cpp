#include "siliscope/PreprocessorCheck.h"
#include "siliscope/Report.h"

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>
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
  if (!ii) {
    return false;
  }
  if (mi->getParameterNum(ii) >= 0) {
    return true;
  }
  return mi->isVariadic() && ii->isStr("__VA_ARGS__");
}

// #param and param ## other cannot take parentheses without changing the tokens.
bool hashOrPaste(const MacroInfo *mi, unsigned index) {
  const unsigned n = mi->getNumTokens();
  if (index > 0) {
    const Token &prev = mi->getReplacementToken(index - 1);
    if (prev.is(clang::tok::hash) || prev.is(clang::tok::hashhash)) {
      return true;
    }
  }
  return index + 1 < n && mi->getReplacementToken(index + 1).is(clang::tok::hashhash);
}

bool paramParenthesized(const MacroInfo *mi, unsigned index) {
  const unsigned n = mi->getNumTokens();
  if (index == 0 || index + 1 >= n) {
    return false;
  }
  return mi->getReplacementToken(index - 1).is(clang::tok::l_paren) &&
         mi->getReplacementToken(index + 1).is(clang::tok::r_paren);
}

// The outer pair has to cover the whole replacement list, not just the first term.
bool bodyWrapped(const MacroInfo *mi) {
  const unsigned n = mi->getNumTokens();
  if (n == 0) {
    return true;
  }
  if (!mi->getReplacementToken(0).is(clang::tok::l_paren) ||
      !mi->getReplacementToken(n - 1).is(clang::tok::r_paren)) {
    return false;
  }
  int depth = 0;
  for (unsigned i = 0; i < n; ++i) {
    const Token &tok = mi->getReplacementToken(i);
    if (tok.is(clang::tok::l_paren)) {
      ++depth;
    } else if (tok.is(clang::tok::r_paren)) {
      --depth;
      if (depth < 0 || (depth == 0 && i + 1 != n)) {
        return false;
      }
    }
  }
  return depth == 0;
}

// A statement body cannot be parenthesized and stay a statement.
bool statementBody(const MacroInfo *mi) {
  if (mi->getNumTokens() == 0) {
    return false;
  }
  const Token &tok = mi->getReplacementToken(0);
  return tok.isOneOf(clang::tok::l_brace,
                     clang::tok::kw_do,
                     clang::tok::kw_while,
                     clang::tok::kw_for,
                     clang::tok::kw_if,
                     clang::tok::kw_switch,
                     clang::tok::kw_return,
                     clang::tok::kw_goto,
                     clang::tok::kw_break,
                     clang::tok::kw_continue,
                     clang::tok::kw_case,
                     clang::tok::kw_default,
                     clang::tok::kw_try,
                     clang::tok::kw_throw,
                     clang::tok::kw_asm);
}

std::string spliceLines(llvm::StringRef in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\\' && i + 1 < in.size()) {
      if (in[i + 1] == '\n') {
        ++i;
        continue;
      }
      if (in[i + 1] == '\r') {
        ++i;
        if (i + 1 < in.size() && in[i + 1] == '\n') {
          ++i;
        }
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

bool identChar(char c, bool first) {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$') {
    return true;
  }
  return !first && c >= '0' && c <= '9';
}

llvm::StringRef readIdent(const std::string &text, size_t &i) {
  if (i >= text.size() || !identChar(text[i], true)) {
    return {};
  }
  const size_t begin = i++;
  while (i < text.size() && identChar(text[i], false)) {
    ++i;
  }
  return llvm::StringRef(text.data() + begin, i - begin);
}

void skipBlockComment(const std::string &text, size_t &i) {
  i += 2;
  while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
    ++i;
  }
  if (i + 1 < text.size()) {
    i += 2;
  }
}

// Returns false when the directive is over (end of line or a line comment).
bool skipWsInDirective(const std::string &text, size_t &i) {
  while (i < text.size()) {
    const char c = text[i];
    if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
      ++i;
      continue;
    }
    if (c == '\n' || c == '\r') {
      return false;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      return false;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      skipBlockComment(text, i);
      continue;
    }
    return true;
  }
  return false;
}

void finishDirective(const std::string &text, size_t &i) {
  while (i < text.size()) {
    if (text[i] == '\n') {
      ++i;
      return;
    }
    if (text[i] == '\r') {
      ++i;
      if (i < text.size() && text[i] == '\n') {
        ++i;
      }
      return;
    }
    if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n' && text[i] != '\r') {
        ++i;
      }
      continue;
    }
    if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      skipBlockComment(text, i);
      continue;
    }
    ++i;
  }
}

// #ifndef NAME / #define NAME / #endif around the whole file, or #pragma once.
// #if !defined is a different directive and does not count.
bool headerGuarded(llvm::StringRef input) {
  std::string text = spliceLines(input);
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
    text.erase(0, 3);
  }

  enum class Phase { Open, Guard, Closed, NotGuard };
  Phase phase = Phase::Open;
  int depth = 0;
  bool once = false;
  bool defined = false;
  std::string guard;
  bool bol = true;

  auto onCode = [&] {
    if (phase == Phase::Open && depth == 0) {
      phase = Phase::NotGuard;
    }
    if (phase == Phase::Closed) {
      phase = Phase::NotGuard;
    }
    bol = false;
  };

  auto onOpen = [&](llvm::StringRef name, bool ifndef) {
    if (phase == Phase::Open && depth == 0) {
      if (ifndef && !name.empty()) {
        phase = Phase::Guard;
        guard.assign(name.data(), name.size());
      } else {
        phase = Phase::NotGuard;
      }
    }
    ++depth;
  };

  auto onOther = [&] {
    if (phase == Phase::Open && depth == 0) {
      phase = Phase::NotGuard;
    }
    if (phase == Phase::Closed) {
      phase = Phase::NotGuard;
    }
  };

  for (size_t i = 0; i < text.size();) {
    const char c = text[i];
    if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
      ++i;
      continue;
    }
    if (c == '\n') {
      ++i;
      bol = true;
      continue;
    }
    if (c == '\r') {
      ++i;
      if (i < text.size() && text[i] == '\n') {
        ++i;
      }
      bol = true;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n' && text[i] != '\r') {
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      skipBlockComment(text, i);
      continue;
    }
    if (!(bol && c == '#')) {
      if (c == '"' || c == '\'') {
        onCode();
        const char quote = c;
        ++i;
        while (i < text.size() && text[i] != '\n' && text[i] != '\r') {
          if (text[i] == '\\' && i + 1 < text.size()) {
            i += 2;
            continue;
          }
          if (text[i] == quote) {
            ++i;
            break;
          }
          ++i;
        }
        continue;
      }
      onCode();
      ++i;
      continue;
    }

    ++i;
    if (!skipWsInDirective(text, i)) {
      onOther();
      finishDirective(text, i);
      bol = true;
      continue;
    }
    const llvm::StringRef kw = readIdent(text, i);
    if (kw == "ifndef" || kw == "ifdef" || kw == "if") {
      llvm::StringRef name;
      if (skipWsInDirective(text, i)) {
        name = readIdent(text, i);
      }
      onOpen(name, kw == "ifndef");
    } else if (kw == "endif") {
      if (depth > 0) {
        --depth;
      }
      if (phase == Phase::Guard && depth == 0) {
        phase = Phase::Closed;
      }
    } else if (kw == "define") {
      llvm::StringRef name;
      if (skipWsInDirective(text, i)) {
        name = readIdent(text, i);
      }
      if (phase == Phase::Guard && depth > 0 && name == guard) {
        defined = true;
      } else {
        onOther();
      }
    } else if (kw == "pragma") {
      llvm::StringRef name;
      if (skipWsInDirective(text, i)) {
        name = readIdent(text, i);
      }
      if (name == "once" && depth == 0) {
        once = true;
      } else {
        onOther();
      }
    } else {
      onOther();
    }
    finishDirective(text, i);
    bol = true;
  }
  return once || (phase == Phase::Closed && defined);
}

constexpr size_t kNotRaw = static_cast<size_t>(-1);

bool digitChar(char c) {
  return c >= '0' && c <= '9';
}

bool rawDChar(char c) {
  return c != ' ' && c != '(' && c != ')' && c != '\\' && c != '\t' && c != '\v' && c != '\f' &&
         c != '\n' && c != '\r';
}

bool rawStringPrefix(llvm::StringRef ident) {
  return ident == "R" || ident == "LR" || ident == "uR" || ident == "UR" || ident == "u8R";
}

void consumeEscaped(llvm::StringRef buf, size_t &i) {
  ++i;
  if (i >= buf.size()) {
    return;
  }
  if (buf[i] == '\r') {
    ++i;
    if (i < buf.size() && buf[i] == '\n') {
      ++i;
    }
    return;
  }
  ++i;
}

// An unescaped newline ends a literal. The rest of the file stays visible.
void scanQuoted(llvm::StringRef buf, size_t &i, char quote) {
  ++i;
  while (i < buf.size()) {
    if (buf[i] == '\\') {
      consumeEscaped(buf, i);
      continue;
    }
    if (buf[i] == '\n' || buf[i] == '\r') {
      return;
    }
    if (buf[i] == quote) {
      ++i;
      return;
    }
    ++i;
  }
}

// i is the opening quote of a C++ raw string. kNotRaw means it is an ordinary string.
size_t finishRawString(llvm::StringRef buf, size_t quote) {
  size_t i = quote + 1;
  const size_t begin = i;
  while (i < buf.size() && rawDChar(buf[i]) && i - begin < 16) {
    ++i;
  }
  if (i >= buf.size() || buf[i] != '(') {
    return kNotRaw;
  }
  const llvm::StringRef delim = buf.substr(begin, i - begin);
  ++i;
  while (i < buf.size()) {
    if (buf[i] == ')') {
      const size_t tail = i + 1;
      if (tail + delim.size() < buf.size() && buf.substr(tail, delim.size()) == delim &&
          buf[tail + delim.size()] == '"') {
        return tail + delim.size() + 1;
      }
    }
    ++i;
  }
  return buf.size();
}

void scanPpNumber(llvm::StringRef buf, size_t &i) {
  ++i;
  while (i < buf.size()) {
    const char c = buf[i];
    if (c == '\'' && i + 1 < buf.size() && (digitChar(buf[i + 1]) || identChar(buf[i + 1], true))) {
      i += 2;
      continue;
    }
    if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') && i + 1 < buf.size() &&
        (buf[i + 1] == '+' || buf[i + 1] == '-')) {
      i += 2;
      continue;
    }
    if (digitChar(c) || identChar(c, true) || c == '.') {
      ++i;
      continue;
    }
    break;
  }
}

// Physical source, not the lexer. Phase 2 deletes a backslash-newline before
// comments exist, and a comment handler never sees a comment inside #if 0.
template <typename Note>
void scanLineComment(llvm::StringRef buf, size_t &i, Note &note) {
  i += 2;
  while (i < buf.size()) {
    const char c = buf[i];
    if (c == '\n' || c == '\r') {
      return;
    }
    if (c == '/' && i + 1 < buf.size() && (buf[i + 1] == '/' || buf[i + 1] == '*')) {
      note(static_cast<unsigned>(i),
           buf[i + 1] == '/' ? "a comment contains //" : "a comment contains /*");
      i += 2;
      continue;
    }
    if (c == '\\' && i + 1 < buf.size() && (buf[i + 1] == '\n' || buf[i + 1] == '\r')) {
      note(static_cast<unsigned>(i), "a // comment ends in a backslash");
      ++i;
      if (buf[i] == '\r') {
        ++i;
        if (i < buf.size() && buf[i] == '\n') {
          ++i;
        }
      } else {
        ++i;
      }
      continue;
    }
    ++i;
  }
}

template <typename Note>
void scanBlockComment(llvm::StringRef buf, size_t &i, Note &note) {
  i += 2;
  while (i < buf.size()) {
    if (buf[i] == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
      i += 2;
      return;
    }
    if (buf[i] == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      note(static_cast<unsigned>(i), "a comment contains //");
      i += 2;
      continue;
    }
    // The * of /*/ closes the comment, so that pair is not a nested /*.
    if (buf[i] == '/' && i + 1 < buf.size() && buf[i + 1] == '*' &&
        (i + 2 >= buf.size() || buf[i + 2] != '/')) {
      note(static_cast<unsigned>(i), "a comment contains /*");
      i += 2;
      continue;
    }
    ++i;
  }
}

template <typename Note>
void scanCommentTokens(llvm::StringRef buf, bool cxx, Note note) {
  for (size_t i = 0; i < buf.size();) {
    const char c = buf[i];
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      scanLineComment(buf, i, note);
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      scanBlockComment(buf, i, note);
      continue;
    }
    if (c == '"' || c == '\'') {
      scanQuoted(buf, i, c);
      continue;
    }
    if (identChar(c, true)) {
      const size_t begin = i++;
      while (i < buf.size() && identChar(buf[i], false)) {
        ++i;
      }
      if (cxx && i < buf.size() && buf[i] == '"') {
        const llvm::StringRef ident(buf.data() + begin, i - begin);
        if (rawStringPrefix(ident)) {
          const size_t after = finishRawString(buf, i);
          if (after == kNotRaw) {
            scanQuoted(buf, i, '"');
          } else {
            i = after;
          }
        }
      }
      continue;
    }
    if (digitChar(c) || (c == '.' && i + 1 < buf.size() && digitChar(buf[i + 1]))) {
      scanPpNumber(buf, i);
      continue;
    }
    ++i;
  }
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
      checkIncludeGuard(PrevFID);
      checkCommentTokens(PrevFID);
    }
  }

  void EndOfMainFile() override {
    const std::vector<OpenCond> leftover = std::move(opens);
    opens.clear();
    for (const OpenCond &open : leftover) {
      reportUnclosed(open.loc);
    }
    checkCommentTokens(pp.getSourceManager().getMainFileID());
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
    checkParens(mi, MacroNameTok, sm);
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

  bool projectFile(FileID file, bool allowMain) const {
    const SourceManager &sm = pp.getSourceManager();
    if (file.isInvalid() || (!allowMain && file == sm.getMainFileID())) {
      return false;
    }
    const SourceLocation start = sm.getLocForStartOfFile(file);
    return start.isValid() && !sm.isInSystemHeader(start) && !sm.isWrittenInBuiltinFile(start) &&
           !sm.isWrittenInScratchSpace(start) && !sm.isInPredefinedFile(start) &&
           !sm.isWrittenInCommandLineFile(start);
  }

  void checkIncludeGuard(FileID file) {
    if (!projectFile(file, false)) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    const SourceLocation start = sm.getLocForStartOfFile(file);
    bool invalid = false;
    const llvm::StringRef buf = sm.getBufferData(file, &invalid);
    if (invalid || headerGuarded(buf)) {
      return;
    }
    reporter.emit(sm, start, "ss.pre.include-guard", "give this header an include guard");
  }

  void checkCommentTokens(FileID file) {
    if (!projectFile(file, true)) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    bool invalid = false;
    const llvm::StringRef buf = sm.getBufferData(file, &invalid);
    if (invalid || buf.empty()) {
      return;
    }
    const SourceLocation start = sm.getLocForStartOfFile(file);
    scanCommentTokens(buf, pp.getLangOpts().CPlusPlus, [&](unsigned offset, const char *msg) {
      reporter.emit(
          sm, start.getLocWithOffset(static_cast<int>(offset)), "ss.pre.comment-tokens", msg);
    });
  }

  void checkParens(const MacroInfo *mi, const Token &nameTok, const SourceManager &sm) {
    const unsigned n = mi->getNumTokens();
    if (n == 0) {
      return;
    }
    if (!bodyWrapped(mi) && !statementBody(mi)) {
      reporter.emit(
          sm, nameTok.getLocation(), "ss.pre.macro-parens", "wrap this macro body in parentheses");
    }
    for (unsigned i = 0; i < n; ++i) {
      const Token &tok = mi->getReplacementToken(i);
      if (!isParameter(mi, tok) || hashOrPaste(mi, i) || paramParenthesized(mi, i)) {
        continue;
      }
      reporter.emit(
          sm, tok.getLocation(), "ss.pre.macro-parens", "wrap this macro parameter in parentheses");
    }
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

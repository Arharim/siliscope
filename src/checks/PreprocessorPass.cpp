#include "siliscope/PreprocessorCheck.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using clang::FileEntry;
using clang::FileID;
using clang::IdentifierInfo;
using clang::MacroDefinition;
using clang::MacroDirective;
using clang::MacroInfo;
using clang::NamedDecl;
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

// Hash offset of the first directive when it is #ifndef. npos otherwise.
// Offsets match the raw file, so they line up with a spelling location.
size_t openingIfndefHash(llvm::StringRef buf) {
  bool bol = true;
  for (size_t i = 0; i < buf.size();) {
    const char c = buf[i];
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
      if (i < buf.size() && buf[i] == '\n') {
        ++i;
      }
      bol = true;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      while (i < buf.size() && buf[i] != '\n' && buf[i] != '\r') {
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      bool atBol = false;
      i += 2;
      while (i + 1 < buf.size() && !(buf[i] == '*' && buf[i + 1] == '/')) {
        if (buf[i] == '\n' || buf[i] == '\r') {
          atBol = true;
        } else if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\v' && buf[i] != '\f') {
          atBol = false;
        }
        ++i;
      }
      if (i + 1 < buf.size()) {
        i += 2;
      }
      bol = atBol;
      continue;
    }
    if (!(bol && c == '#')) {
      return llvm::StringRef::npos;
    }
    const size_t hash = i++;
    while (i < buf.size() && (buf[i] == ' ' || buf[i] == '\t')) {
      ++i;
    }
    const size_t begin = i;
    while (i < buf.size() &&
           (identChar(buf[i], false) || (i == begin && identChar(buf[i], true)))) {
      ++i;
    }
    if (llvm::StringRef(buf.data() + begin, i - begin) == "ifndef") {
      return hash;
    }
    return llvm::StringRef::npos;
  }
  return llvm::StringRef::npos;
}

bool commentText(llvm::StringRef body) {
  for (const char c : body) {
    if (c != ' ' && c != '\t' && c != '\v' && c != '\f' && c != '\r' && c != '\n' && c != '*' &&
        c != '/') {
      return true;
    }
  }
  return false;
}

void scanQuoted(llvm::StringRef buf, size_t &i, char quote);

// A non-empty // or /* */ comment in this slice of the file.
bool regionHasComment(llvm::StringRef region) {
  for (size_t i = 0; i < region.size();) {
    const char c = region[i];
    if (c == '"' || c == '\'') {
      scanQuoted(region, i, c);
      continue;
    }
    if (c == '/' && i + 1 < region.size() && region[i + 1] == '/') {
      return commentText(region.substr(i + 2));
    }
    if (c == '/' && i + 1 < region.size() && region[i + 1] == '*') {
      size_t end = i + 2;
      while (end + 1 < region.size() && !(region[end] == '*' && region[end + 1] == '/')) {
        ++end;
      }
      if (commentText(region.substr(i + 2, end - (i + 2)))) {
        return true;
      }
      i = end + (end + 1 < region.size() ? 2 : 0);
      if (i == end) {
        break;
      }
      continue;
    }
    ++i;
  }
  return false;
}

// The physical line above the directive, when that line is itself a comment.
// A code line that only contains a comment does not count, and neither does
// an empty // or /* */.
bool previousLineExplains(llvm::StringRef buf, unsigned offset) {
  if (static_cast<size_t>(offset) > buf.size()) {
    return false;
  }
  size_t line = offset;
  while (line > 0 && buf[line - 1] != '\n' && buf[line - 1] != '\r') {
    --line;
  }
  if (line == 0) {
    return false;
  }
  size_t end = line;
  if (end > 0 && buf[end - 1] == '\n') {
    --end;
  }
  if (end > 0 && buf[end - 1] == '\r') {
    --end;
  }
  size_t begin = end;
  while (begin > 0 && buf[begin - 1] != '\n' && buf[begin - 1] != '\r') {
    --begin;
  }
  const llvm::StringRef text = buf.substr(begin, end - begin).trim();
  if (text.starts_with("//")) {
    return commentText(text.substr(2));
  }
  if (text.starts_with("/*")) {
    const size_t close = text.find("*/");
    if (close == llvm::StringRef::npos) {
      return commentText(text.substr(2));
    }
    if (!text.substr(close + 2).trim().empty()) {
      return false;
    }
    return commentText(text.substr(2, close < 2 ? 0 : close - 2));
  }
  if (text.starts_with("*")) {
    return commentText(text.substr(1));
  }
  return false;
}

bool conditionExplained(llvm::StringRef buf, unsigned offset) {
  if (static_cast<size_t>(offset) >= buf.size()) {
    return false;
  }
  size_t end = offset;
  while (end < buf.size() && buf[end] != '\n' && buf[end] != '\r') {
    ++end;
  }
  if (regionHasComment(buf.substr(offset, end - offset))) {
    return true;
  }
  return previousLineExplains(buf, offset);
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

bool atWord(llvm::StringRef text, llvm::StringRef word) {
  if (!text.starts_with(word)) {
    return false;
  }
  return text.size() == word.size() || !identChar(text[word.size()], false);
}

llvm::StringRef afterWord(llvm::StringRef text, llvm::StringRef word) {
  return text.drop_front(word.size()).trim();
}

// A disabled directive or statement. A sentence that merely mentions a keyword
// stays a comment: "if the flag is set", "return the status", "for example".
bool codeLine(llvm::StringRef text) {
  text = text.trim();
  if (text.empty()) {
    return false;
  }
  if (text.starts_with("#")) {
    const llvm::StringRef dir = text.drop_front().trim();
    return atWord(dir, "define") || atWord(dir, "include") || atWord(dir, "include_next") ||
           atWord(dir, "if") || atWord(dir, "ifdef") || atWord(dir, "ifndef") ||
           atWord(dir, "elif") || atWord(dir, "else") || atWord(dir, "endif") ||
           atWord(dir, "undef") || atWord(dir, "pragma") || atWord(dir, "error") ||
           atWord(dir, "warning") || atWord(dir, "line");
  }
  if (text == "{" || text == "}" || text == "};") {
    return true;
  }
  if (atWord(text, "if") || atWord(text, "for") || atWord(text, "while") ||
      atWord(text, "switch")) {
    const llvm::StringRef word = text.starts_with("switch")  ? "switch"
                                 : text.starts_with("while") ? "while"
                                 : text.starts_with("for")   ? "for"
                                                             : "if";
    return afterWord(text, word).starts_with("(");
  }
  if (atWord(text, "else")) {
    const llvm::StringRef rest = afterWord(text, "else");
    return rest.empty() || rest.starts_with("{") || atWord(rest, "if");
  }
  if (atWord(text, "do")) {
    const llvm::StringRef rest = afterWord(text, "do");
    return rest.empty() || rest.starts_with("{");
  }
  if (atWord(text, "case") || atWord(text, "default")) {
    return text.contains(':');
  }
  const bool semi = text.ends_with(";");
  if (atWord(text, "return") || atWord(text, "goto") || atWord(text, "break") ||
      atWord(text, "continue") || atWord(text, "throw") || atWord(text, "delete")) {
    const llvm::StringRef word = text.starts_with("continue") ? "continue"
                                 : text.starts_with("return") ? "return"
                                 : text.starts_with("break")  ? "break"
                                 : text.starts_with("throw")  ? "throw"
                                 : text.starts_with("delete") ? "delete"
                                                              : "goto";
    const llvm::StringRef rest = afterWord(text, word);
    return rest.empty() || semi;
  }
  if (atWord(text, "struct") || atWord(text, "union") || atWord(text, "enum") ||
      atWord(text, "class") || atWord(text, "typedef") || atWord(text, "namespace")) {
    return text.contains('{') || semi;
  }
  if (!semi && !text.ends_with("{")) {
    return false;
  }
  if (text.front() == '(' || text.front() == '*' || text.front() == '&') {
    return true;
  }
  if (!identChar(text.front(), true)) {
    return false;
  }
  size_t i = 1;
  while (i < text.size() && identChar(text[i], false)) {
    ++i;
  }
  const llvm::StringRef rest = text.drop_front(i).trim();
  return rest.starts_with("(") || rest.starts_with("=") || rest.starts_with("+") ||
         rest.starts_with("-") || rest.starts_with("[") || rest.starts_with(".") ||
         rest.starts_with("&") || rest.starts_with("*") || rest.starts_with("{") ||
         (!rest.empty() && identChar(rest.front(), true));
}

// A block comment is deleted code only when every line is code. A doc block
// that shows a snippet next to a sentence stays quiet.
bool commentedOutCode(llvm::StringRef body, bool block) {
  bool any = false;
  for (size_t i = 0; i < body.size();) {
    size_t end = i;
    while (end < body.size() && body[end] != '\n' && body[end] != '\r') {
      ++end;
    }
    llvm::StringRef line = body.substr(i, end - i).trim();
    if (block && !line.empty() && line.front() == '*' &&
        (line.size() == 1 || line[1] == ' ' || line[1] == '\t')) {
      line = line.drop_front().trim();
    }
    if (!line.empty()) {
      const bool code = codeLine(line);
      if (block && !code) {
        return false;
      }
      if (!block && code) {
        return true;
      }
      any = code || any;
    }
    i = end;
    if (i < body.size() && body[i] == '\r') {
      ++i;
    }
    if (i < body.size() && body[i] == '\n') {
      ++i;
    }
  }
  return block && any;
}

// Physical source, not the lexer. Phase 2 deletes a backslash-newline before
// comments exist, and a comment handler never sees a comment inside #if 0.
template <typename Note, typename Code>
void scanLineComment(llvm::StringRef buf, size_t &i, Note &note, Code &code) {
  const unsigned at = static_cast<unsigned>(i);
  i += 2;
  const size_t body = i;
  while (i < buf.size()) {
    const char c = buf[i];
    if (c == '\n' || c == '\r') {
      break;
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
  code(at, buf.substr(body, i - body), false);
}

template <typename Note, typename Code>
void scanBlockComment(llvm::StringRef buf, size_t &i, Note &note, Code &code) {
  const unsigned at = static_cast<unsigned>(i);
  i += 2;
  const size_t body = i;
  while (i < buf.size()) {
    if (buf[i] == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
      code(at, buf.substr(body, i - body), true);
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
  code(at, buf.substr(body, i - body), true);
}

template <typename Note, typename Code>
void scanCommentTokens(llvm::StringRef buf, bool cxx, Note note, Code code) {
  for (size_t i = 0; i < buf.size();) {
    const char c = buf[i];
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      scanLineComment(buf, i, note, code);
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      scanBlockComment(buf, i, note, code);
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
  Pass(const Preprocessor &pp, Reporter &reporter) : pp(pp), reporter(reporter), uses(*this) {}

  void InclusionDirective(SourceLocation HashLoc,
                          const Token &,
                          llvm::StringRef FileName,
                          bool IsAngled,
                          clang::CharSourceRange FilenameRange,
                          clang::OptionalFileEntryRef File,
                          llvm::StringRef,
                          llvm::StringRef,
                          const clang::Module *,
                          bool ModuleImported,
                          clang::SrcMgr::CharacteristicKind) override {
    noteMainInclude(HashLoc, FileName, FilenameRange, File, ModuleImported, IsAngled);
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

  void If(SourceLocation Loc, clang::SourceRange, ConditionValueKind) override {
    noteOpen(Loc);
    noteLimited(Loc);
  }

  void Ifdef(SourceLocation Loc, const Token &, const MacroDefinition &MD) override {
    noteOpen(Loc);
    noteLimited(Loc);
    creditMacro(MD, Loc);
  }

  void Ifndef(SourceLocation Loc, const Token &, const MacroDefinition &MD) override {
    noteOpen(Loc);
    noteLimited(Loc);
    creditMacro(MD, Loc);
  }

  void Elif(SourceLocation Loc, clang::SourceRange, ConditionValueKind, SourceLocation) override {
    noteLimited(Loc);
  }

  void Elifdef(SourceLocation Loc, const Token &, const MacroDefinition &MD) override {
    noteLimited(Loc);
    creditMacro(MD, Loc);
  }

  void Elifndef(SourceLocation Loc, const Token &, const MacroDefinition &MD) override {
    noteLimited(Loc);
    creditMacro(MD, Loc);
  }

  void Defined(const Token &, const MacroDefinition &MD, clang::SourceRange Range) override {
    creditMacro(MD, Range.getBegin());
  }

  void MacroExpands(const Token &,
                    const MacroDefinition &MD,
                    clang::SourceRange Range,
                    const clang::MacroArgs *) override {
    creditMacro(MD, Range.getBegin());
  }

  // A skipped include contributed no tokens. A second direct include of a file
  // this file already entered is unused. A direct include of a file that an
  // earlier header pulled in still counts once the main file uses that file.
  void FileSkipped(const clang::FileEntryRef &SkippedFile,
                   const Token &,
                   clang::SrcMgr::CharacteristicKind) override {
    if (pending < 0) {
      return;
    }
    const unsigned index = static_cast<unsigned>(pending);
    pending = -1;
    const FileEntry *file = &SkippedFile.getFileEntry();
    const auto ownerIt = includedBy.find(file);
    if (ownerIt == includedBy.end()) {
      alsoProvides[file].push_back(index);
      return;
    }
    const DirectInclude &owner = includes[ownerIt->second];
    if (owner.file == file) {
      return;
    }
    alsoProvides[file].push_back(index);
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

  void LexedFileChanged(FileID FID,
                        LexedFileChangeReason Reason,
                        clang::SrcMgr::CharacteristicKind,
                        FileID PrevFID,
                        SourceLocation Loc) override {
    if (Reason == LexedFileChangeReason::EnterFile) {
      noteEntered(FID, PrevFID, Loc);
      return;
    }
    if (Reason == LexedFileChangeReason::ExitFile) {
      closeFile(PrevFID);
      checkIncludeGuard(PrevFID);
      checkCommentTokens(PrevFID);
      flushLimited(PrevFID);
    }
  }

  void EndOfMainFile() override {
    const std::vector<OpenCond> leftover = std::move(opens);
    opens.clear();
    for (const OpenCond &open : leftover) {
      reportUnclosed(open.loc);
    }
    const FileID main = pp.getSourceManager().getMainFileID();
    checkCommentTokens(main);
    checkOwnHeader();
    flushLimited(main);
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
    if (mi) {
      checkLimitedMacro(mi, MacroNameTok, sm);
    }
    if (!mi || !mi->isFunctionLike()) {
      return;
    }
    checkPreferInline(mi, MacroNameTok, sm);
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

  struct Cond {
    FileID file;
    SourceLocation loc;
    unsigned offset = 0;
  };

  void noteLimited(SourceLocation loc) {
    if (loc.isInvalid()) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    const SourceLocation spell = sm.getSpellingLoc(loc);
    if (spell.isInvalid()) {
      return;
    }
    const FileID file = sm.getFileID(spell);
    if (!projectFile(file, true)) {
      return;
    }
    conds.push_back({file, spell, sm.getDecomposedLoc(spell).second});
  }

  // ## is paste. # stringize is not. A replacement identifier that is the
  // macro itself is recursive; a parameter of the same name is not.
  // --allow names the macro and silences both.
  void checkLimitedMacro(const MacroInfo *mi, const Token &nameTok, const SourceManager &sm) {
    const IdentifierInfo *nameII = nameTok.getIdentifierInfo();
    if (nameII) {
      const std::string name = nameII->getName().str();
      if (reporter.allows("ss.pre.limited", name.c_str())) {
        return;
      }
    }
    SourceLocation pasteAt;
    bool pasted = false;
    bool recursive = false;
    const unsigned n = mi->getNumTokens();
    for (unsigned i = 0; i < n; ++i) {
      const Token &tok = mi->getReplacementToken(i);
      if (!pasted && tok.is(clang::tok::hashhash)) {
        pasted = true;
        pasteAt = tok.getLocation();
      }
      if (!recursive && nameII && tok.getIdentifierInfo() == nameII && !isParameter(mi, tok)) {
        recursive = true;
      }
    }
    if (pasted) {
      if (pasteAt.isInvalid()) {
        pasteAt = nameTok.getLocation();
      }
      reporter.emit(sm, pasteAt, "ss.pre.limited", "do not paste tokens");
    }
    if (recursive) {
      reporter.emit(sm, nameTok.getLocation(), "ss.pre.limited", "do not write a recursive macro");
    }
  }

  // The opening #ifndef of a classic guard needs no comment. #pragma once
  // does not exempt a later #ifndef, and #if !defined is not a guard.
  void flushLimited(FileID file) {
    if (file.isInvalid() || conds.empty()) {
      return;
    }
    std::vector<Cond> mine;
    std::vector<Cond> kept;
    mine.reserve(conds.size());
    kept.reserve(conds.size());
    for (const Cond &cond : conds) {
      if (cond.file == file) {
        mine.push_back(cond);
      } else {
        kept.push_back(cond);
      }
    }
    conds.swap(kept);
    if (mine.empty() || !projectFile(file, true)) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    bool invalid = false;
    const llvm::StringRef buf = sm.getBufferData(file, &invalid);
    if (invalid) {
      return;
    }
    unsigned guardLine = 0;
    if (headerGuarded(buf)) {
      const size_t hash = openingIfndefHash(buf);
      if (hash != llvm::StringRef::npos) {
        guardLine = sm.getLineNumber(file, static_cast<unsigned>(hash));
      }
    }
    for (const Cond &cond : mine) {
      if (guardLine != 0 && sm.getLineNumber(file, cond.offset) == guardLine) {
        continue;
      }
      if (conditionExplained(buf, cond.offset)) {
        continue;
      }
      reporter.emit(sm, cond.loc, "ss.pre.limited", "say why this condition is here");
    }
  }

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
    scanCommentTokens(
        buf,
        pp.getLangOpts().CPlusPlus,
        [&](unsigned offset, const char *msg) {
          reporter.emit(
              sm, start.getLocWithOffset(static_cast<int>(offset)), "ss.pre.comment-tokens", msg);
        },
        [&](unsigned offset, llvm::StringRef body, bool block) {
          if (!commentedOutCode(body, block)) {
            return;
          }
          reporter.emit(sm,
                        start.getLocWithOffset(static_cast<int>(offset)),
                        "ss.pre.no-commented-code",
                        "deleted code belongs in version control");
        });
  }

  // A function cannot stringize or paste. #param is also how a macro names
  // a header for #include.
  bool mustStayMacro(const MacroInfo *mi) const {
    const unsigned n = mi->getNumTokens();
    for (unsigned i = 0; i < n; ++i) {
      const Token &tok = mi->getReplacementToken(i);
      if (tok.is(clang::tok::hash) || tok.is(clang::tok::hashhash)) {
        return true;
      }
    }
    return false;
  }

  void checkPreferInline(const MacroInfo *mi, const Token &nameTok, const SourceManager &sm) {
    if (mustStayMacro(mi)) {
      return;
    }
    if (const IdentifierInfo *ii = nameTok.getIdentifierInfo()) {
      const std::string name = ii->getName().str();
      if (reporter.allows("ss.pre.prefer-inline", name.c_str())) {
        return;
      }
    }
    reporter.emit(sm,
                  nameTok.getLocation(),
                  "ss.pre.prefer-inline",
                  "write this as a static inline function");
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

  struct DirectInclude {
    SourceLocation at;
    std::string name;
    const FileEntry *file = nullptr;
    bool used = false;
    bool angled = false;
  };

  // A direct include of the main file is used when that file spells a macro,
  // type, or declaration the include brought in. A later definition in the
  // main file counts as a use of an earlier declaration in the header.
  // --allow takes the include name as written, without quotes or brackets.
  class Uses final : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    explicit Uses(Pass &owner) : owner(owner) {}

    void credit(const NamedDecl *decl) { owner.creditDecl(decl); }

    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override {
      if (done) {
        return;
      }
      const auto *tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
      if (!tu) {
        return;
      }
      done = true;
      Walker walker(*this);
      walker.TraverseDecl(const_cast<clang::TranslationUnitDecl *>(tu));
      owner.emitUnusedIncludes();
    }

  private:
    class Walker : public clang::RecursiveASTVisitor<Walker> {
    public:
      explicit Walker(Uses &uses) : uses(uses) {}

      bool VisitDeclRefExpr(clang::DeclRefExpr *expr) {
        if (uses.owner.inMain(expr->getLocation())) {
          uses.credit(expr->getDecl());
        }
        return true;
      }

      bool VisitMemberExpr(clang::MemberExpr *expr) {
        if (uses.owner.inMain(expr->getMemberLoc())) {
          uses.credit(expr->getMemberDecl());
        }
        return true;
      }

      bool VisitCXXConstructExpr(clang::CXXConstructExpr *expr) {
        if (uses.owner.inMain(expr->getLocation())) {
          uses.credit(expr->getConstructor());
        }
        return true;
      }

      bool VisitTypeLoc(clang::TypeLoc loc) {
        if (!uses.owner.inMain(loc.getBeginLoc()) || loc.isNull()) {
          return true;
        }
        const clang::Type *type = loc.getType().getTypePtrOrNull();
        if (!type) {
          return true;
        }
        if (const auto *alias = clang::dyn_cast<clang::TypedefType>(type)) {
          uses.credit(alias->getDecl());
        } else if (const auto *tag = clang::dyn_cast<clang::TagType>(type)) {
          uses.credit(tag->getDecl());
        } else if (const auto *spec = clang::dyn_cast<clang::TemplateSpecializationType>(type)) {
          if (clang::TemplateDecl *pattern = spec->getTemplateName().getAsTemplateDecl()) {
            uses.credit(pattern);
          }
        } else if (const auto *used = clang::dyn_cast<clang::UsingType>(type)) {
          if (const clang::UsingShadowDecl *shadow = used->getDecl()) {
            uses.credit(shadow->getTargetDecl());
          }
        }
        return true;
      }

      bool VisitFunctionDecl(clang::FunctionDecl *decl) {
        if (decl->isImplicit() || !decl->isThisDeclarationADefinition() ||
            !uses.owner.inMain(decl->getLocation())) {
          return true;
        }
        for (clang::FunctionDecl *other : decl->redecls()) {
          uses.credit(other);
        }
        if (const auto *method = clang::dyn_cast<clang::CXXMethodDecl>(decl)) {
          uses.credit(method->getParent());
        }
        return true;
      }

      bool VisitVarDecl(clang::VarDecl *decl) {
        if (decl->isImplicit() || !decl->isThisDeclarationADefinition() ||
            !uses.owner.inMain(decl->getLocation())) {
          return true;
        }
        for (clang::VarDecl *other : decl->redecls()) {
          uses.credit(other);
        }
        return true;
      }

      bool VisitTagDecl(clang::TagDecl *decl) {
        if (decl->isImplicit() || !decl->isThisDeclarationADefinition() ||
            !uses.owner.inMain(decl->getLocation())) {
          return true;
        }
        for (clang::TagDecl *other : decl->redecls()) {
          uses.credit(other);
        }
        return true;
      }

    private:
      Uses &uses;
    };

    Pass &owner;
    bool done = false;
  };

public:
  void watch(clang::ast_matchers::MatchFinder &finder) {
    finder.addMatcher(clang::ast_matchers::translationUnitDecl().bind("tu"), &uses);
  }

  bool inMain(SourceLocation loc) const {
    if (loc.isInvalid()) {
      return false;
    }
    const SourceManager &sm = pp.getSourceManager();
    loc = sm.getSpellingLoc(loc);
    if (loc.isInvalid() || sm.isWrittenInScratchSpace(loc) || sm.isInPredefinedFile(loc) ||
        sm.isWrittenInBuiltinFile(loc) || sm.isWrittenInCommandLineFile(loc)) {
      return false;
    }
    return sm.getFileID(loc) == sm.getMainFileID();
  }

  void noteMainInclude(SourceLocation hash,
                       llvm::StringRef name,
                       clang::CharSourceRange filename,
                       clang::OptionalFileEntryRef file,
                       bool moduleImported,
                       bool angled) {
    if (moduleImported || !file || !inMain(hash)) {
      return;
    }
    SourceLocation at = filename.getBegin();
    if (at.isInvalid()) {
      at = hash;
    }
    includes.push_back(DirectInclude{at, name.str(), nullptr, false, angled});
    pending = static_cast<int>(includes.size() - 1);
  }

  // foo.c includes "foo.h" before any other header, so the header is parsed
  // on its own. A quoted include whose filename is foo.h counts, including
  // "inc/foo.h". Angle brackets do not. With no such include, the rule fires
  // only when foo.h sits next to foo.c. --allow names that header.
  void checkOwnHeader() {
    if (pp.getLangOpts().CPlusPlus) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    const SourceLocation start = sm.getLocForStartOfFile(sm.getMainFileID());
    if (start.isInvalid()) {
      return;
    }
    const llvm::StringRef path(sm.getFilename(start));
    llvm::StringRef base = llvm::sys::path::filename(path);
    if (!base.consume_back(".c")) {
      return;
    }
    const std::string header = (base + ".h").str();
    if (reporter.allows("ss.pre.source-includes-own-header", header.c_str())) {
      return;
    }
    int own = -1;
    for (unsigned i = 0; i < includes.size(); ++i) {
      const DirectInclude &include = includes[i];
      if (include.angled) {
        continue;
      }
      if (llvm::sys::path::filename(include.name) == header) {
        own = static_cast<int>(i);
        break;
      }
    }
    if (own == 0) {
      return;
    }
    if (own > 0) {
      reporter.emit(sm,
                    includes[static_cast<unsigned>(own)].at,
                    "ss.pre.source-includes-own-header",
                    "include this file's own header first");
      return;
    }
    llvm::SmallString<256> sibling(path);
    llvm::sys::path::remove_filename(sibling);
    llvm::sys::path::append(sibling, header);
    if (!llvm::sys::fs::exists(sibling)) {
      return;
    }
    const SourceLocation at = includes.empty() ? start : includes[0].at;
    reporter.emit(
        sm, at, "ss.pre.source-includes-own-header", "include this file's own header first");
  }

  void noteEntered(FileID fid, FileID prev, SourceLocation loc) {
    const SourceManager &sm = pp.getSourceManager();
    const FileEntry *file = sm.getFileEntryForID(fid);
    if (!file) {
      pending = -1;
      return;
    }
    const bool fromMain = prev == sm.getMainFileID() || inMain(loc);
    if (pending >= 0 && fromMain) {
      const unsigned index = static_cast<unsigned>(pending);
      pending = -1;
      includes[index].file = file;
      if (!includedBy.try_emplace(file, index).second) {
        alsoProvides[file].push_back(index);
      }
      return;
    }
    pending = -1;
    const FileEntry *parent = sm.getFileEntryForID(prev);
    if (!parent) {
      return;
    }
    const auto parentIt = includedBy.find(parent);
    if (parentIt == includedBy.end()) {
      return;
    }
    includedBy.try_emplace(file, parentIt->second);
  }

  void creditMacro(const MacroDefinition &definition, SourceLocation use) {
    if (!inMain(use)) {
      return;
    }
    const MacroInfo *info = definition.getMacroInfo();
    if (!info) {
      return;
    }
    creditLoc(info->getDefinitionLoc());
  }

  void creditDecl(const NamedDecl *decl) {
    if (!decl || decl->isImplicit()) {
      return;
    }
    if (const auto *first = clang::dyn_cast<NamedDecl>(decl->getCanonicalDecl())) {
      creditLoc(first->getLocation());
    }
    creditLoc(decl->getLocation());
  }

  void creditLoc(SourceLocation loc) {
    if (loc.isInvalid()) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    loc = sm.getSpellingLoc(loc);
    if (loc.isInvalid() || sm.isWrittenInScratchSpace(loc) || sm.isInPredefinedFile(loc) ||
        sm.isWrittenInBuiltinFile(loc) || sm.isWrittenInCommandLineFile(loc)) {
      return;
    }
    creditFile(sm.getFileID(loc));
  }

  void creditFile(FileID fid) {
    const FileEntry *file = pp.getSourceManager().getFileEntryForID(fid);
    if (!file) {
      return;
    }
    const auto ownerIt = includedBy.find(file);
    if (ownerIt != includedBy.end()) {
      includes[ownerIt->second].used = true;
    }
    const auto extra = alsoProvides.find(file);
    if (extra == alsoProvides.end()) {
      return;
    }
    for (const unsigned index : extra->second) {
      includes[index].used = true;
    }
  }

  void emitUnusedIncludes() {
    const SourceManager &sm = pp.getSourceManager();
    for (const DirectInclude &include : includes) {
      if (include.used) {
        continue;
      }
      if (reporter.allows("ss.pre.no-unused-include", include.name.c_str())) {
        continue;
      }
      reporter.emit(sm, include.at, "ss.pre.no-unused-include", "this include is not used");
    }
  }

  const Preprocessor &pp;
  Reporter &reporter;
  std::vector<OpenCond> opens;
  std::vector<Cond> conds;
  std::vector<DirectInclude> includes;
  llvm::DenseMap<const FileEntry *, unsigned> includedBy;
  llvm::DenseMap<const FileEntry *, llvm::SmallVector<unsigned, 1>> alsoProvides;
  int pending = -1;
  Uses uses;
};

} // namespace

void PreprocessorCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void PreprocessorCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

void attachPreprocessorPass(Preprocessor &pp,
                            Reporter &reporter,
                            clang::ast_matchers::MatchFinder &finder) {
  auto pass = std::make_unique<Pass>(pp, reporter);
  Pass *raw = pass.get();
  pp.addPPCallbacks(std::move(pass));
  raw->watch(finder);
}

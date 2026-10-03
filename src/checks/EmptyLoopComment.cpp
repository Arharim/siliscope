#include "siliscope/EmptyLoopComment.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"

#include <cctype>

using clang::ASTContext;
using clang::AttributedStmt;
using clang::CompoundStmt;
using clang::CXXForRangeStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Stmt;
using clang::WhileStmt;
using clang::ast_matchers::cxxForRangeStmt;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::whileStmt;

namespace {

constexpr const char *kId = "ss.ctrl.empty-loop-comment";

bool isVacuous(const Stmt *stmt) {
  if (!stmt || llvm::isa<clang::NullStmt>(stmt)) {
    return true;
  }
  if (const auto *comp = llvm::dyn_cast<CompoundStmt>(stmt)) {
    for (const Stmt *child : comp->body()) {
      if (!isVacuous(child)) {
        return false;
      }
    }
    return true;
  }
  if (const auto *attr = llvm::dyn_cast<AttributedStmt>(stmt)) {
    return isVacuous(attr->getSubStmt());
  }
  return false;
}

const Stmt *bareBody(const Stmt *stmt) {
  while (const auto *attr = llvm::dyn_cast_or_null<AttributedStmt>(stmt)) {
    stmt = attr->getSubStmt();
  }
  return stmt;
}

// Quoted text is not a comment, so a URL in a condition does not document the wait.
bool hasComment(llvm::StringRef text) {
  const size_t n = text.size();
  for (size_t i = 0; i < n; ++i) {
    const char c = text[i];
    if (c == '"' || c == '\'') {
      const char quote = c;
      ++i;
      while (i < n && text[i] != quote && text[i] != '\n') {
        if (text[i] == '\\' && i + 1 < n) {
          i += 2;
          continue;
        }
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < n && (text[i + 1] == '/' || text[i + 1] == '*')) {
      return true;
    }
  }
  return false;
}

struct Buf {
  clang::FileID fid;
  llvm::StringRef text;
  bool ok = false;
};

Buf bufferOf(SourceLocation loc, const SourceManager &sm) {
  Buf out;
  if (loc.isInvalid()) {
    return out;
  }
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid()) {
    return out;
  }
  bool invalid = false;
  out.fid = sm.getFileID(loc);
  out.text = sm.getBufferData(out.fid, &invalid);
  out.ok = !invalid;
  return out;
}

unsigned spellingOffset(SourceLocation loc, const SourceManager &sm, clang::FileID fid, bool &ok) {
  ok = false;
  if (loc.isInvalid()) {
    return 0;
  }
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid() || sm.getFileID(loc) != fid) {
    return 0;
  }
  ok = true;
  return sm.getFileOffset(loc);
}

// Comment block immediately above the keyword, plus the loop's own lines.
// A comment on the following line belongs to the next statement.
unsigned commentStart(llvm::StringRef buf, unsigned kw) {
  unsigned pos = kw > buf.size() ? static_cast<unsigned>(buf.size()) : kw;
  while (pos > 0) {
    const unsigned char prev = static_cast<unsigned char>(buf[pos - 1]);
    if (std::isspace(prev)) {
      --pos;
      continue;
    }
    if (pos >= 2 && buf[pos - 2] == '*' && buf[pos - 1] == '/') {
      const unsigned end = pos;
      unsigned star = pos - 2;
      bool found = false;
      while (star > 0) {
        --star;
        if (buf[star] == '/' && buf[star + 1] == '*') {
          found = true;
          break;
        }
      }
      if (!found || star >= end) {
        break;
      }
      pos = star;
      continue;
    }
    unsigned line = pos;
    while (line > 0 && buf[line - 1] != '\n') {
      --line;
    }
    unsigned i = line;
    while (i < pos && std::isspace(static_cast<unsigned char>(buf[i]))) {
      ++i;
    }
    if (i + 1 < buf.size() && buf[i] == '/' && buf[i + 1] == '/' && line < pos) {
      pos = line;
      continue;
    }
    break;
  }
  return pos;
}

unsigned lineEnd(llvm::StringRef buf, unsigned offset) {
  if (offset > buf.size()) {
    offset = static_cast<unsigned>(buf.size());
  }
  const size_t nl = buf.find('\n', offset);
  if (nl == llvm::StringRef::npos) {
    return static_cast<unsigned>(buf.size());
  }
  return static_cast<unsigned>(nl);
}

bool regionHasComment(const Buf &buf, unsigned begin, unsigned end) {
  if (!buf.ok || begin > end || end > buf.text.size()) {
    return false;
  }
  return hasComment(buf.text.substr(begin, end - begin));
}

bool windowHasComment(const Stmt *loop, const SourceManager &sm) {
  const Buf buf = bufferOf(loop->getBeginLoc(), sm);
  bool kwOk = false;
  const unsigned kw = spellingOffset(loop->getBeginLoc(), sm, buf.fid, kwOk);
  if (!buf.ok || !kwOk) {
    return false;
  }
  unsigned endOff = kw;
  bool endOk = false;
  const unsigned spelledEnd = spellingOffset(loop->getEndLoc(), sm, buf.fid, endOk);
  if (endOk && spelledEnd >= kw) {
    endOff = spelledEnd;
  }
  return regionHasComment(buf, commentStart(buf.text, kw), lineEnd(buf.text, endOff));
}

// Braces spelled in a macro live in that macro, which may sit above the call.
bool compoundHasComment(const CompoundStmt *comp, const SourceManager &sm) {
  if (!comp) {
    return false;
  }
  const Buf buf = bufferOf(comp->getLBracLoc(), sm);
  bool openOk = false;
  bool closeOk = false;
  const unsigned open = spellingOffset(comp->getLBracLoc(), sm, buf.fid, openOk);
  const unsigned close = spellingOffset(comp->getRBracLoc(), sm, buf.fid, closeOk);
  if (!buf.ok || !openOk || !closeOk || open + 1 > close) {
    return false;
  }
  return regionHasComment(buf, open + 1, close);
}

bool documented(const Stmt *loop, const Stmt *body, const SourceManager &sm) {
  if (windowHasComment(loop, sm)) {
    return true;
  }
  return compoundHasComment(llvm::dyn_cast<CompoundStmt>(body), sm);
}

// do { } while (0) and while (false) {} are empty statements, not a wait.
// for (;;) has no condition and still spins, so it stays in the rule.
const Expr *loopCond(const Stmt *loop) {
  if (const auto *w = llvm::dyn_cast<WhileStmt>(loop)) {
    return w->getCond();
  }
  if (const auto *d = llvm::dyn_cast<DoStmt>(loop)) {
    return d->getCond();
  }
  if (const auto *f = llvm::dyn_cast<ForStmt>(loop)) {
    return f->getCond();
  }
  return nullptr;
}

bool constantFalse(const Expr *cond, ASTContext &ctx) {
  if (!cond) {
    return false;
  }
  Expr::EvalResult value;
  if (!cond->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
    return false;
  }
  return value.Val.getInt().isZero();
}

} // namespace

void EmptyLoopCommentCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(whileStmt().bind("loop"), this);
  finder.addMatcher(forStmt().bind("loop"), this);
  finder.addMatcher(doStmt().bind("loop"), this);
  finder.addMatcher(cxxForRangeStmt().bind("loop"), this);
}

void EmptyLoopCommentCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *loop = result.Nodes.getNodeAs<Stmt>("loop");
  if (!loop || !result.SourceManager || !result.Context) {
    return;
  }
  if (constantFalse(loopCond(loop), *result.Context)) {
    return;
  }
  const Stmt *body = nullptr;
  if (const auto *w = llvm::dyn_cast<WhileStmt>(loop)) {
    body = w->getBody();
  } else if (const auto *f = llvm::dyn_cast<ForStmt>(loop)) {
    body = f->getBody();
  } else if (const auto *d = llvm::dyn_cast<DoStmt>(loop)) {
    body = d->getBody();
  } else if (const auto *r = llvm::dyn_cast<CXXForRangeStmt>(loop)) {
    body = r->getBody();
  }
  if (!body || !isVacuous(body)) {
    return;
  }
  const Stmt *bare = bareBody(body);
  const bool braced = llvm::isa<CompoundStmt>(bare);
  if (braced && documented(loop, bare, *result.SourceManager)) {
    return;
  }
  const char *msg = braced ? "an empty loop needs a comment saying why it waits"
                           : "an empty loop needs braces and a comment saying why it waits";
  reporter.emit(*result.SourceManager, loop->getBeginLoc(), kId, msg);
}

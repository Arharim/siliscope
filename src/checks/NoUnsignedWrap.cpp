#include "siliscope/NoUnsignedWrap.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringRef.h"

using clang::BinaryOperator;
using clang::Expr;
using clang::UnaryOperator;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::unaryOperator;

namespace {

static bool constantValue(const Expr *e, clang::ASTContext &ctx, llvm::APSInt &out) {
  if (!e || e->isValueDependent()) {
    return false;
  }
  clang::Expr::EvalResult value;
  if (!e->IgnoreParenImpCasts()->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
    return false;
  }
  out = value.Val.getInt();
  return true;
}

static llvm::StringRef commentBody(llvm::StringRef line) {
  const size_t slash = line.find("//");
  const size_t block = line.find("/*");
  if (block != llvm::StringRef::npos && (slash == llvm::StringRef::npos || block < slash)) {
    llvm::StringRef body = line.substr(block + 2);
    const size_t close = body.find("*/");
    if (close != llvm::StringRef::npos) {
      body = body.take_front(close);
    }
    return body;
  }
  if (slash != llvm::StringRef::npos) {
    return line.substr(slash + 2);
  }
  return {};
}

static bool mentionsMask(llvm::StringRef text) {
  if (text.empty()) {
    return false;
  }
  const std::string lower = text.lower();
  return llvm::StringRef(lower).contains("mask");
}

// The wrap is a mask when this line says so, or the previous physical line
// is itself a comment that says so. An identifier named mask is not a comment.
static bool documentedMask(const clang::SourceManager &sm, clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  const std::pair<clang::FileID, unsigned> where = sm.getDecomposedLoc(loc);
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(where.first, &invalid);
  if (invalid || where.second > buf.size()) {
    return false;
  }
  size_t begin = where.second;
  while (begin > 0 && buf[begin - 1] != '\n' && buf[begin - 1] != '\r') {
    --begin;
  }
  size_t end = where.second;
  while (end < buf.size() && buf[end] != '\n' && buf[end] != '\r') {
    ++end;
  }
  if (mentionsMask(commentBody(buf.substr(begin, end - begin)))) {
    return true;
  }
  if (begin == 0) {
    return false;
  }
  size_t prevEnd = begin;
  if (prevEnd > 0 && buf[prevEnd - 1] == '\n') {
    --prevEnd;
  }
  if (prevEnd > 0 && buf[prevEnd - 1] == '\r') {
    --prevEnd;
  }
  size_t prevBegin = prevEnd;
  while (prevBegin > 0 && buf[prevBegin - 1] != '\n' && buf[prevBegin - 1] != '\r') {
    --prevBegin;
  }
  const llvm::StringRef prev = buf.substr(prevBegin, prevEnd - prevBegin).trim();
  if (prev.starts_with("*")) {
    return mentionsMask(prev.substr(1));
  }
  if (!prev.starts_with("//") && !prev.starts_with("/*")) {
    return false;
  }
  return mentionsMask(commentBody(prev));
}

static bool unsignedWidth(clang::QualType t, clang::ASTContext &ctx, unsigned &bits) {
  if (t.isNull() || !t->isUnsignedIntegerType()) {
    return false;
  }
  bits = ctx.getIntWidth(t);
  return bits > 0 && bits <= 128;
}

static bool addOrMulWraps(llvm::APSInt lhs, llvm::APSInt rhs, unsigned bits, bool multiply) {
  const llvm::APInt wideL = lhs.zextOrTrunc(bits).zext(bits * 2);
  const llvm::APInt wideR = rhs.zextOrTrunc(bits).zext(bits * 2);
  const llvm::APInt product = multiply ? wideL * wideR : wideL + wideR;
  return product.getActiveBits() > bits;
}

static bool subWraps(llvm::APSInt lhs, llvm::APSInt rhs, unsigned bits) {
  const llvm::APInt wideL = lhs.zextOrTrunc(bits);
  const llvm::APInt wideR = rhs.zextOrTrunc(bits);
  return wideL.ult(wideR);
}

} // namespace

void NoUnsignedWrapCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("bin"), this);
  finder.addMatcher(unaryOperator(clang::ast_matchers::hasOperatorName("-")).bind("neg"), this);
}

void NoUnsignedWrapCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  clang::ASTContext &ctx = *result.Context;
  const clang::SourceManager &sm = *result.SourceManager;
  clang::SourceLocation loc;
  bool wraps = false;
  if (const auto *bin = result.Nodes.getNodeAs<BinaryOperator>("bin")) {
    const auto op = bin->getOpcode();
    if (op != clang::BO_Add && op != clang::BO_Sub && op != clang::BO_Mul) {
      return;
    }
    unsigned bits = 0;
    llvm::APSInt lhs;
    llvm::APSInt rhs;
    if (!unsignedWidth(bin->getType(), ctx, bits) || !constantValue(bin->getLHS(), ctx, lhs) ||
        !constantValue(bin->getRHS(), ctx, rhs)) {
      return;
    }
    if (op == clang::BO_Sub) {
      wraps = subWraps(lhs, rhs, bits);
    } else {
      wraps = addOrMulWraps(lhs, rhs, bits, op == clang::BO_Mul);
    }
    loc = bin->getOperatorLoc();
  } else if (const auto *neg = result.Nodes.getNodeAs<UnaryOperator>("neg")) {
    unsigned bits = 0;
    llvm::APSInt value;
    if (!unsignedWidth(neg->getType(), ctx, bits) ||
        !constantValue(neg->getSubExpr(), ctx, value)) {
      return;
    }
    const llvm::APInt truncated = value.zextOrTrunc(bits);
    wraps = !truncated.isZero();
    loc = neg->getOperatorLoc();
  }
  if (!wraps || documentedMask(sm, loc)) {
    return;
  }
  reporter.emit(sm, loc, "ss.conv.no-unsigned-wrap-const", "unsigned constant wraps");
}

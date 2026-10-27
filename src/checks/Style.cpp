#include "siliscope/Style.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"

using clang::ASTContext;
using clang::BinaryOperator;
using clang::CallExpr;
using clang::CompoundStmt;
using clang::DeclRefExpr;
using clang::DoStmt;
using clang::Expr;
using clang::FileID;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SwitchStmt;
using clang::Token;
using clang::TypedefDecl;
using clang::VarDecl;
using clang::WhileStmt;

namespace {

constexpr unsigned kWidth = 80;

bool projectFile(const SourceManager &sm, FileID file) {
  if (file.isInvalid()) {
    return false;
  }
  const SourceLocation start = sm.getLocForStartOfFile(file);
  return start.isValid() && !sm.isInSystemHeader(start) && !sm.isWrittenInBuiltinFile(start) &&
         !sm.isWrittenInScratchSpace(start) && !sm.isInPredefinedFile(start) &&
         !sm.isWrittenInCommandLineFile(start);
}

bool projectLoc(const SourceManager &sm, SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  return loc.isValid() && projectFile(sm, sm.getFileID(loc));
}

bool snake(llvm::StringRef name) {
  if (name.empty()) {
    return false;
  }
  const char front = name.front();
  if (front != '_' && (front < 'a' || front > 'z')) {
    return false;
  }
  for (const char c : name) {
    if (c != '_' && (c < 'a' || c > 'z') && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

bool upperMacro(llvm::StringRef name) {
  if (name.empty()) {
    return false;
  }
  const char front = name.front();
  if (front != '_' && (front < 'A' || front > 'Z')) {
    return false;
  }
  for (const char c : name) {
    if (c != '_' && (c < 'A' || c > 'Z') && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

bool typedefName(llvm::StringRef name) {
  return name.ends_with("_t") && snake(name);
}

// A column is a byte. A tab stops at the next multiple of 8.
void scanText(const SourceManager &sm, FileID file, llvm::StringRef buf, Reporter &reporter) {
  const SourceLocation origin = sm.getLocForStartOfFile(file);
  const auto at = [&](unsigned offset) {
    return origin.getLocWithOffset(static_cast<int>(offset));
  };
  unsigned column = 0;
  unsigned lineStart = 0;
  unsigned indent = 0;
  bool sawContent = false;
  bool tabIndent = false;
  bool tabReported = false;
  bool tooWide = false;
  unsigned wideAt = 0;
  bool lineComment = false;
  bool block = false;
  char quote = 0;
  int paren = 0;
  int bracket = 0;
  unsigned semis = 0;

  const auto finish = [&] {
    if (semis >= 2) {
      reporter.emit(sm, at(lineStart), "ss.style.one-stmt", "put one statement on this line");
    }
    if (tooWide) {
      reporter.emit(sm, at(wideAt), "ss.style.line-width", "keep this line to 80 columns");
    }
    column = 0;
    indent = 0;
    sawContent = false;
    tabIndent = false;
    tabReported = false;
    tooWide = false;
    semis = 0;
    lineComment = false;
  };

  for (size_t i = 0; i < buf.size(); ++i) {
    const char c = buf[i];
    if (c == '\n') {
      finish();
      lineStart = static_cast<unsigned>(i + 1);
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (quote) {
      if (c == '\\' && i + 1 < buf.size() && buf[i + 1] != '\n') {
        ++i;
        column += 2;
        if (column > kWidth && !tooWide) {
          tooWide = true;
          wideAt = static_cast<unsigned>(i - 1);
        }
        continue;
      }
      if (c == quote) {
        quote = 0;
      }
      ++column;
      if (column > kWidth && !tooWide) {
        tooWide = true;
        wideAt = static_cast<unsigned>(i);
      }
      continue;
    }
    if (lineComment) {
      ++column;
      if (column > kWidth && !tooWide) {
        tooWide = true;
        wideAt = static_cast<unsigned>(i);
      }
      continue;
    }
    if (block) {
      if (c == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
        block = false;
        ++i;
        column += 2;
      } else {
        ++column;
      }
      if (column > kWidth && !tooWide) {
        tooWide = true;
        wideAt = static_cast<unsigned>(i);
      }
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      lineComment = true;
      ++i;
      column += 2;
      if (column > kWidth && !tooWide) {
        tooWide = true;
        wideAt = static_cast<unsigned>(i - 1);
      }
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      block = true;
      ++i;
      column += 2;
      if (column > kWidth && !tooWide) {
        tooWide = true;
        wideAt = static_cast<unsigned>(i - 1);
      }
      continue;
    }
    if (!sawContent && (c == ' ' || c == '\t')) {
      if (c == '\t') {
        tabIndent = true;
        if (!tabReported) {
          reporter.emit(sm, at(static_cast<unsigned>(i)), "ss.style.no-tabs", "indent with spaces");
          tabReported = true;
        }
        column = (column + 8U) & ~7U;
      } else {
        ++indent;
        ++column;
      }
      if (column > kWidth && !tooWide) {
        tooWide = true;
        wideAt = static_cast<unsigned>(i);
      }
      continue;
    }
    if (!sawContent) {
      sawContent = true;
      // A directive stays at the left edge. Other lines step by 4 spaces.
      if (c != '#' && (tabIndent || indent % 4U != 0)) {
        reporter.emit(sm, at(lineStart), "ss.style.indent-4", "indent by a multiple of 4 spaces");
      }
    }
    if (c == '\t') {
      if (!tabReported) {
        reporter.emit(sm, at(static_cast<unsigned>(i)), "ss.style.no-tabs", "indent with spaces");
        tabReported = true;
      }
      column = (column + 8U) & ~7U;
    } else {
      ++column;
    }
    if (column > kWidth && !tooWide) {
      tooWide = true;
      wideAt = static_cast<unsigned>(i);
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') {
      ++paren;
    } else if (c == ')' && paren > 0) {
      --paren;
    } else if (c == '[') {
      ++bracket;
    } else if (c == ']' && bracket > 0) {
      --bracket;
    } else if (c == ';' && paren == 0 && bracket == 0) {
      ++semis;
    }
  }
  finish();
}

bool hasGuard(llvm::StringRef buf) {
  bool lineStart = true;
  for (size_t i = 0; i < buf.size(); ++i) {
    const char c = buf[i];
    if (c == '\n') {
      lineStart = true;
      continue;
    }
    if (lineStart && (c == ' ' || c == '\t' || c == '\r')) {
      continue;
    }
    if (lineStart && c == '#') {
      const llvm::StringRef rest = buf.substr(i + 1).ltrim(" \t");
      if (rest.starts_with("ifndef")) {
        return true;
      }
      if (rest.starts_with("pragma") && rest.substr(6).ltrim(" \t").starts_with("once")) {
        return true;
      }
      if (rest.starts_with("if")) {
        const llvm::StringRef cond = rest.substr(2).ltrim(" \t");
        if (cond.starts_with("!defined") || cond.starts_with("! defined")) {
          return true;
        }
      }
    }
    lineStart = false;
  }
  return false;
}

void checkBrace(const SourceManager &sm,
                SourceLocation key,
                SourceLocation brace,
                Reporter &reporter,
                const char *allowName) {
  if (!projectLoc(sm, key) || !projectLoc(sm, brace)) {
    return;
  }
  key = sm.getSpellingLoc(key);
  brace = sm.getSpellingLoc(brace);
  if (sm.getFileID(key) != sm.getFileID(brace)) {
    return;
  }
  if (sm.getSpellingLineNumber(brace) > sm.getSpellingLineNumber(key) &&
      sm.getSpellingColumnNumber(brace) == sm.getSpellingColumnNumber(key)) {
    return;
  }
  if (reporter.allows("ss.style.brace-own-line", allowName)) {
    return;
  }
  reporter.emit(sm, brace, "ss.style.brace-own-line", "put the opening brace on the next line");
}

bool constantSide(const Expr *expr) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  if (llvm::isa<clang::IntegerLiteral>(expr) || llvm::isa<clang::FloatingLiteral>(expr) ||
      llvm::isa<clang::CharacterLiteral>(expr) || llvm::isa<clang::StringLiteral>(expr) ||
      llvm::isa<clang::CXXBoolLiteralExpr>(expr)) {
    return true;
  }
  if (const auto *unary = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
    const auto op = unary->getOpcode();
    if (op == clang::UO_Plus || op == clang::UO_Minus || op == clang::UO_Not ||
        op == clang::UO_LNot) {
      return constantSide(unary->getSubExpr());
    }
    return false;
  }
  const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr);
  if (!ref || !ref->getDecl()) {
    return false;
  }
  if (llvm::isa<clang::EnumConstantDecl>(ref->getDecl())) {
    return true;
  }
  const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl());
  return var && var->getType().isConstQualified();
}

const FunctionDecl *asFunction(const Expr *expr) {
  if (!expr) {
    return nullptr;
  }
  expr = expr->IgnoreParenImpCasts();
  if (const auto *unary = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
    if (unary->getOpcode() == clang::UO_AddrOf) {
      return asFunction(unary->getSubExpr());
    }
  }
  const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr);
  if (!ref) {
    return nullptr;
  }
  return llvm::dyn_cast<FunctionDecl>(ref->getDecl());
}

bool threadApi(llvm::StringRef name) {
  return name.contains_insensitive("task") || name.contains_insensitive("thread");
}

class Pass : public clang::PPCallbacks {
  struct QuotedInclude {
    std::string name;
    bool angled = false;
  };

  class Walk : public clang::RecursiveASTVisitor<Walk> {
  public:
    Walk(Pass &owner, ASTContext &ctx) : owner(owner), ctx(ctx), sm(ctx.getSourceManager()) {}

    bool VisitFunctionDecl(FunctionDecl *fn) {
      if (!fn || fn->isImplicit() || !fn->getIdentifier() || !projectLoc(sm, fn->getLocation())) {
        return true;
      }
      if (const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(fn)) {
        if (method->getParent() && method->getParent()->isLambda()) {
          return true;
        }
      }
      if (!llvm::isa<clang::CXXConstructorDecl>(fn) && !llvm::isa<clang::CXXDestructorDecl>(fn) &&
          !llvm::isa<clang::CXXConversionDecl>(fn) && !fn->isOverloadedOperator()) {
        const llvm::StringRef name = fn->getName();
        if (!snake(name) && !owner.reporter.allows("ss.style.snake-case", name.data())) {
          owner.reporter.emit(
              sm, fn->getLocation(), "ss.style.snake-case", "use snake_case for this function");
        }
      }
      if (isIsr(fn) && !fn->getName().ends_with("_isr") &&
          !owner.reporter.allows("ss.emb.isr-naming", fn->getName().data())) {
        owner.reporter.emit(
            sm, fn->getLocation(), "ss.emb.isr-naming", "end this interrupt handler with _isr");
      }
      if (!fn->isThisDeclarationADefinition() || fn->isDefaulted() || fn->isDeleted()) {
        return true;
      }
      const auto *body = llvm::dyn_cast_or_null<CompoundStmt>(fn->getBody());
      if (!body) {
        return true;
      }
      checkBrace(sm, fn->getBeginLoc(), body->getLBracLoc(), owner.reporter, fn->getName().data());
      return true;
    }

    bool VisitVarDecl(VarDecl *var) {
      if (!var || var->isImplicit() || !var->getIdentifier() ||
          !projectLoc(sm, var->getLocation())) {
        return true;
      }
      const llvm::StringRef name = var->getName();
      if (!snake(name) && !owner.reporter.allows("ss.style.snake-case", name.data())) {
        owner.reporter.emit(
            sm, var->getLocation(), "ss.style.snake-case", "use snake_case for this variable");
      }
      if (ctx.getLangOpts().CPlusPlus) {
        return true;
      }
      if (var->getStorageClass() != clang::SC_Register && !writtenAuto(var)) {
        return true;
      }
      if (owner.reporter.allows("ss.style.no-register-auto", name.data())) {
        return true;
      }
      owner.reporter.emit(
          sm, var->getBeginLoc(), "ss.style.no-register-auto", "do not use register or auto");
      return true;
    }

    bool VisitFieldDecl(clang::FieldDecl *field) {
      if (!field || field->isImplicit() || !field->getIdentifier() ||
          !projectLoc(sm, field->getLocation())) {
        return true;
      }
      const llvm::StringRef name = field->getName();
      if (!snake(name) && !owner.reporter.allows("ss.style.snake-case", name.data())) {
        owner.reporter.emit(
            sm, field->getLocation(), "ss.style.snake-case", "use snake_case for this variable");
      }
      return true;
    }

    bool VisitTypedefDecl(TypedefDecl *decl) {
      if (!decl || ctx.getLangOpts().CPlusPlus || !decl->getIdentifier() ||
          !projectLoc(sm, decl->getLocation())) {
        return true;
      }
      const llvm::StringRef name = decl->getName();
      if (owner.reporter.allows("ss.style.type-suffix-t", name.data())) {
        return true;
      }
      const SourceLocation loc = sm.getSpellingLoc(decl->getLocation());
      if (!typedefName(name)) {
        owner.reporter.emit(
            sm, loc, "ss.style.type-suffix-t", "name this typedef in lowercase and end it with _t");
        return true;
      }
      const llvm::StringRef file = sm.getFilename(loc);
      if (llvm::sys::path::extension(file) != ".h") {
        return true;
      }
      const std::string prefix = (llvm::sys::path::stem(file) + "_").str();
      if (!prefix.empty() && prefix != "_" && !name.starts_with(prefix)) {
        owner.reporter.emit(
            sm, loc, "ss.style.type-suffix-t", "prefix this public typedef with the module name");
      }
      return true;
    }

    bool VisitBinaryOperator(BinaryOperator *op) {
      if (!op || op->getOpcode() != clang::BO_EQ || !projectLoc(sm, op->getOperatorLoc())) {
        return true;
      }
      if (constantSide(op->getLHS()) || !constantSide(op->getRHS())) {
        return true;
      }
      owner.reporter.emit(
          sm, op->getOperatorLoc(), "ss.style.yoda", "put the constant on the left of ==");
      return true;
    }

    bool VisitCallExpr(CallExpr *call) {
      if (!call || !projectLoc(sm, call->getBeginLoc())) {
        return true;
      }
      const clang::Decl *callee = call->getCalleeDecl();
      const auto *named = llvm::dyn_cast_or_null<clang::NamedDecl>(callee);
      if (!named || !named->getIdentifier() || !threadApi(named->getName())) {
        return true;
      }
      for (const Expr *arg : call->arguments()) {
        const FunctionDecl *entry = asFunction(arg);
        if (!entry || !entry->getIdentifier()) {
          continue;
        }
        const llvm::StringRef name = entry->getName();
        if (name.ends_with("_task") || name.ends_with("_thread")) {
          continue;
        }
        if (owner.reporter.allows("ss.emb.thread-naming", name.data())) {
          continue;
        }
        const SourceLocation loc = arg ? arg->getBeginLoc() : call->getBeginLoc();
        owner.reporter.emit(
            sm, loc, "ss.emb.thread-naming", "name this thread entry *_task or *_thread");
      }
      return true;
    }

    bool VisitIfStmt(IfStmt *stmt) {
      braceOf(stmt ? stmt->getIfLoc() : SourceLocation(), stmt ? stmt->getThen() : nullptr, "if");
      if (stmt && stmt->getElse() && !llvm::isa<IfStmt>(stmt->getElse())) {
        braceOf(stmt->getElseLoc(), stmt->getElse(), "else");
      }
      return true;
    }

    bool VisitForStmt(ForStmt *stmt) {
      braceOf(stmt ? stmt->getForLoc() : SourceLocation(), stmt ? stmt->getBody() : nullptr, "for");
      return true;
    }

    bool VisitWhileStmt(WhileStmt *stmt) {
      braceOf(
          stmt ? stmt->getWhileLoc() : SourceLocation(), stmt ? stmt->getBody() : nullptr, "while");
      return true;
    }

    bool VisitDoStmt(DoStmt *stmt) {
      braceOf(stmt ? stmt->getDoLoc() : SourceLocation(), stmt ? stmt->getBody() : nullptr, "do");
      return true;
    }

    bool VisitSwitchStmt(SwitchStmt *stmt) {
      braceOf(stmt ? stmt->getSwitchLoc() : SourceLocation(),
              stmt ? stmt->getBody() : nullptr,
              "switch");
      return true;
    }

  private:
    bool writtenAuto(const VarDecl *var) const {
      SourceLocation loc = sm.getSpellingLoc(var->getBeginLoc());
      Token tok;
      if (loc.isInvalid() ||
          clang::Lexer::getRawToken(loc, tok, sm, ctx.getLangOpts(), /*IgnoreWhiteSpace=*/true)) {
        return false;
      }
      return tok.is(clang::tok::kw_auto);
    }

    void braceOf(SourceLocation key, const clang::Stmt *body, const char *allowName) {
      const auto *compound = llvm::dyn_cast_or_null<CompoundStmt>(body);
      if (!compound) {
        return;
      }
      checkBrace(sm, key, compound->getLBracLoc(), owner.reporter, allowName);
    }

    Pass &owner;
    ASTContext &ctx;
    const SourceManager &sm;
  };

public:
  Pass(const clang::Preprocessor &pp, Reporter &reporter)
      : pp(pp), reporter(reporter), onTu(*this) {}

  void InclusionDirective(SourceLocation HashLoc,
                          const Token &,
                          llvm::StringRef FileName,
                          bool IsAngled,
                          clang::CharSourceRange,
                          clang::OptionalFileEntryRef,
                          llvm::StringRef,
                          llvm::StringRef,
                          const clang::Module *,
                          bool,
                          clang::SrcMgr::CharacteristicKind) override {
    if (HashLoc.isInvalid()) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    const SourceLocation spell = sm.getSpellingLoc(HashLoc);
    if (!projectLoc(sm, spell) || sm.getFileID(spell) != sm.getMainFileID()) {
      return;
    }
    includes.push_back(QuotedInclude{FileName.str(), IsAngled});
  }

  void MacroDefined(const Token &name, const clang::MacroDirective *) override {
    const clang::IdentifierInfo *id = name.getIdentifierInfo();
    if (!id) {
      return;
    }
    const SourceManager &sm = pp.getSourceManager();
    if (!projectLoc(sm, name.getLocation())) {
      return;
    }
    const llvm::StringRef spelling = id->getName();
    if (upperMacro(spelling) || reporter.allows("ss.style.snake-case", spelling.data())) {
      return;
    }
    reporter.emit(sm, name.getLocation(), "ss.style.snake-case", "spell this macro in UPPER_CASE");
  }

  void watch(clang::ast_matchers::MatchFinder &finder) {
    finder.addMatcher(clang::ast_matchers::translationUnitDecl().bind("tu"), &onTu);
  }

  void finish(ASTContext &ctx) {
    const SourceManager &sm = ctx.getSourceManager();
    for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
      const FileID file = sm.translateFile(it->getFirst());
      if (!projectFile(sm, file)) {
        continue;
      }
      bool invalid = false;
      const llvm::StringRef buf = sm.getBufferData(file, &invalid);
      if (!invalid && !buf.empty()) {
        scanText(sm, file, buf, reporter);
      }
    }
    Walk walk(*this, ctx);
    walk.TraverseDecl(ctx.getTranslationUnitDecl());
    checkModule(ctx);
  }

private:
  class OnTu : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    explicit OnTu(Pass &owner) : owner(owner) {}

    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override {
      if (!result.Context || result.Context == seen) {
        return;
      }
      seen = result.Context;
      owner.finish(*result.Context);
    }

  private:
    Pass &owner;
    const ASTContext *seen = nullptr;
  };

  void checkModule(ASTContext &ctx) {
    if (ctx.getLangOpts().CPlusPlus) {
      return;
    }
    const SourceManager &sm = ctx.getSourceManager();
    const SourceLocation start = sm.getLocForStartOfFile(sm.getMainFileID());
    if (!projectLoc(sm, start)) {
      return;
    }
    llvm::StringRef base = llvm::sys::path::filename(sm.getFilename(start));
    if (!base.consume_back(".c")) {
      return;
    }
    const std::string header = (base + ".h").str();
    if (reporter.allows("ss.style.module-files", header.c_str())) {
      return;
    }
    bool included = false;
    for (const QuotedInclude &inc : includes) {
      if (!inc.angled && llvm::sys::path::filename(inc.name) == header) {
        included = true;
        break;
      }
    }
    if (!included) {
      reporter.emit(sm, start, "ss.style.module-files", "include this file's own header");
      return;
    }
    for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
      const FileID file = sm.translateFile(it->getFirst());
      if (!projectFile(sm, file)) {
        continue;
      }
      const SourceLocation fileStart = sm.getLocForStartOfFile(file);
      if (llvm::sys::path::filename(sm.getFilename(fileStart)) != header) {
        continue;
      }
      bool invalid = false;
      const llvm::StringRef buf = sm.getBufferData(file, &invalid);
      if (invalid || hasGuard(buf)) {
        return;
      }
      reporter.emit(sm, fileStart, "ss.style.module-files", "guard this header");
      return;
    }
  }

  const clang::Preprocessor &pp;
  Reporter &reporter;
  std::vector<QuotedInclude> includes;
  OnTu onTu;
};

} // namespace

void StyleCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void StyleCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

void attachStylePass(clang::Preprocessor &pp,
                     Reporter &reporter,
                     clang::ast_matchers::MatchFinder &finder) {
  auto pass = std::make_unique<Pass>(pp, reporter);
  Pass *raw = pass.get();
  pp.addPPCallbacks(std::move(pass));
  raw->watch(finder);
}

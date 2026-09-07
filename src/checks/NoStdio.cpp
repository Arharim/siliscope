#include "siliscope/NoStdio.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;

void NoStdioCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("printf",
                                                            "fprintf",
                                                            "sprintf",
                                                            "vsprintf",
                                                            "vprintf",
                                                            "scanf",
                                                            "fscanf",
                                                            "sscanf",
                                                            "fopen",
                                                            "fclose",
                                                            "fread",
                                                            "fwrite",
                                                            "fgets",
                                                            "fputs",
                                                            "puts",
                                                            "putchar",
                                                            "getchar",
                                                            "perror"))))
                        .bind("call"),
                    this);
}

void NoStdioCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *c = result.Nodes.getNodeAs<CallExpr>("call");
  if (!c) {
    return;
  }
  reporter.emit(*result.SourceManager,
                c->getBeginLoc(),
                "warning",
                "ss.libc.no-stdio",
                "do not use stdio in firmware");
}

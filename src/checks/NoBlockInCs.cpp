#include "siliscope/NoBlockInCs.h"

void NoBlockInCsCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void NoBlockInCsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

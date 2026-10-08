#include "siliscope/NoRecursion.h"

void NoRecursionCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void NoRecursionCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

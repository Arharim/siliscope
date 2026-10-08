#include "siliscope/NoHeap.h"

void NoHeapCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void NoHeapCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

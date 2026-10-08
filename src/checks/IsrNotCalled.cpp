#include "siliscope/IsrNotCalled.h"

void IsrNotCalledCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void IsrNotCalledCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

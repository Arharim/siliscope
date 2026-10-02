#pragma once

#include <memory>

class Check;
class Reporter;

// One row in the implemented-checker table. Adding a checker is a row here
// plus the .cpp on the CMake target. --list treats every other catalog id
// as enabled-but-not-run.
struct CheckSpec {
  const char *id;
  std::unique_ptr<Check> (*make)(Reporter &);
};

const CheckSpec *checkSpecs();
unsigned checkSpecCount();
bool hasChecker(const char *id);

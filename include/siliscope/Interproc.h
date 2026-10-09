#pragma once

#include "siliscope/Report.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace clang {
class ASTContext;
class CFG;
class FunctionDecl;
class SourceManager;
} // namespace clang

class Reporter;

// What one function body does to each parameter and to its return value.
// Stored by functionKey so a caller parsed earlier can be resolved at finish.
struct Summary {
  struct Param {
    bool deref = false;
    bool readFirst = false;
    bool writeFirst = false;
    bool clean = true;
    bool poisoned = false;
    bool forward = false;
    std::vector<int64_t> idxs;
    std::string fwdKey;
    unsigned fwdArg = 0;
  };
  struct Ret {
    enum class Kind { None, Null, Param, Forward, Wild } kind = Kind::None;
    unsigned param = 0;
    int64_t off = 0;
    std::string fwdKey;
    // fwdMap[callee param] = this function's param, or -1 when the argument
    // is not that parameter.
    std::vector<int> fwdMap;
  };
  std::vector<Param> params;
  Ret ret;
};

// One direct call, plus uses of its return value and later reads of an
// automatic whose address was passed in.
struct CallArg {
  unsigned index = 0;
  bool nullMust = false;
  bool uninit = false;
  bool bound = false;
  uint64_t size = 0;
  int64_t off = 0;
  std::string name;
};

struct CallNote {
  std::string callee;
  DiagSite at;
  std::string caller;
  std::vector<CallArg> args;
};

struct RetUse {
  enum class Kind { Deref, Index } kind = Kind::Deref;
  uint32_t call = 0;
  int64_t off = 0;
  int64_t index = 0;
  DiagSite at;
  std::string caller;
  std::string name;
};

struct LaterRead {
  std::vector<std::pair<uint32_t, unsigned>> deps;
  DiagSite at;
  std::string caller;
  std::string name;
};

// Facts collected while each translation unit is still alive. finish() runs
// after every file, so a callee parsed later still reaches its callers.
class ProgramFacts {
public:
  enum class ParamEffect { Missing, Opaque, Read, Write, Untouched };

  uint32_t nextId();
  void putSummary(std::string key, Summary summary);
  const Summary *find(const std::string &key) const;
  // Missing means the body is not in the run yet. Opaque means the body does
  // not prove a single effect. Read, Write, and Untouched are must-facts.
  ParamEffect effectOf(const std::string &key, unsigned arg) const;
  void noteCall(uint32_t id, CallNote note);
  void noteRetUse(RetUse use);
  void noteLater(LaterRead use);
  void finish(Reporter &reporter) const;

private:
  struct Touch {
    enum class Kind { Missing, Opaque, Read, Write, Untouched } kind = Kind::Missing;
    bool deref = false;
    std::vector<int64_t> idxs;
  };
  struct Returned {
    enum class Kind { None, Null, Param } kind = Kind::None;
    unsigned param = 0;
    int64_t off = 0;
  };

  Touch resolveParam(const std::string &key,
                     unsigned arg,
                     int depth,
                     std::vector<std::pair<std::string, unsigned>> &seen) const;
  Returned resolveRet(const std::string &key, int depth, std::vector<std::string> &seen) const;
  void emitAt(Reporter &reporter,
              const DiagSite &at,
              const std::string &caller,
              const std::string &name,
              const char *id,
              const char *msg) const;

  uint32_t ids = 0;
  std::map<std::string, Summary> summaries;
  std::vector<CallNote> calls;
  std::vector<RetUse> uses;
  std::vector<LaterRead> laters;
};

Summary summarizeFunction(const clang::FunctionDecl &fn,
                          const clang::CFG &cfg,
                          clang::ASTContext &ctx,
                          const clang::SourceManager &sm);

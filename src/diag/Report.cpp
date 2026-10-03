#include "siliscope/Report.h"

#include "siliscope/Profile.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

Reporter::Reporter(const Profile &profile) : profile(&profile) {}

bool Reporter::SeenDiag::operator<(const SeenDiag &other) const {
  if (file != other.file) {
    return file < other.file;
  }
  if (line != other.line) {
    return line < other.line;
  }
  if (col != other.col) {
    return col < other.col;
  }
  if (id != other.id) {
    return id < other.id;
  }
  return msg < other.msg;
}

void Reporter::emit(const clang::SourceManager &sm,
                    clang::SourceLocation loc,
                    const char *id,
                    const char *msg) {
  const char *severity = profile->severityOf(id);
  if (!severity) {
    return;
  }
  if (loc.isInvalid()) {
    return;
  }
  // Zephyr's LOG_* macros pass their implementation through as arguments, so
  // the expansion location of every token is the call. The spelling location
  // is the file that wrote the token: the call for an argument, the header
  // for the macro itself. Token paste lives in <scratch space>; step out to
  // the macro that pasted it. Repeated expansions of one token collapse below.
  while (loc.isMacroID() && sm.isWrittenInScratchSpace(sm.getSpellingLoc(loc))) {
    const clang::SourceLocation parent = sm.getImmediateMacroCallerLoc(loc);
    if (parent.isInvalid() || parent == loc) {
      break;
    }
    loc = parent;
  }
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid() || sm.isInSystemHeader(loc) || sm.isWrittenInScratchSpace(loc) ||
      sm.isInPredefinedFile(loc)) {
    return;
  }
  const clang::PresumedLoc pl = sm.getPresumedLoc(loc);
  if (pl.isInvalid() || !pl.getFilename()) {
    return;
  }
  const SeenDiag key{pl.getFilename(), pl.getLine(), pl.getColumn(), id, msg};
  if (!seen.insert(key).second) {
    return;
  }
  ++findings;
  llvm::outs() << pl.getFilename() << ":" << pl.getLine() << ":" << pl.getColumn() << ": "
               << severity << ": " << msg << " [" << id << "]\n";
}

void Reporter::printSummary() const {
  llvm::outs() << "findings: " << findings << "\n";
}

bool Reporter::allows(const char *id, const char *name) const {
  return profile->allows(id, name);
}

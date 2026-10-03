/* ss-run: extra=-isystem extra=tests/lit/checks/ss.expr.no-inc-in-expr/sys expect=clean */
#include <bump.h>

static int call(int n) { return bump(n); }

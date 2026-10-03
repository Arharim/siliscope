/* ss-run: allow=ss.pre.prefer-inline:WAIT_FLAG expect=clean */
#include "quiet.h"

/* ordinary block */
// ordinary line
/// documentation
/** documentation */
/* slash then closer /*/
static const char *url(void) { return "https://example.com"; }

static int divide(int a, int b) { return a / b; }

static int ticks(void) { return '/' / '/'; }

/* continued \
   block */

// path c:\temp\data

#define WAIT_FLAG(flag) \
  while (*(flag) == 0) { \
    /* wait */ \
  }

static int use(volatile int *flag) {
  WAIT_FLAG(flag);
  return QUICK;
}

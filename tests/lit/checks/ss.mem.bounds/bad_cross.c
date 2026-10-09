/* ss-run: also=other.c expect=ss.mem.bounds */
#include "call_api.h"

int use(void) {
  int a[4];
  a[0] = 1;
  return read4(a);
}

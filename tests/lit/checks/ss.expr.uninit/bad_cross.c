/* ss-run: also=other.c expect=ss.expr.uninit */
#include "call_api.h"

int use(void) {
  const int x;
  return readp(&x);
}

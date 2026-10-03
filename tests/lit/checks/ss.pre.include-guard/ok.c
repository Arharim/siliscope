#include "guarded.h"
#include "once.h"
#include "nested.h"

static int use(void) {
  return GUARDED + ONCE + NESTED;
}

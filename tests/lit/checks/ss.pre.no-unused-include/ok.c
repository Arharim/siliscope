#include "used.h"
#include "own.h"
#include "nest.h"
#include "inner.h"

static int own_body(void) { return used_value(); }

static int use(void) {
  const used_id value = USED_FLAG;
  return value + own_body() + from_b();
}

/* ss-companion */
#include "ok_api.h"

static void rec(void) {}

void other(void) { rec(); }

void worker(void) { other(); }

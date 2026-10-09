/* ss-run: also=other_allow.c allow=ss.fn.single-definition:use expect=clean */
#include "api.h"

void use(int n) { (void)n; }

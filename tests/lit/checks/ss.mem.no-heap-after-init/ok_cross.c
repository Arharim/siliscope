/* ss-run: also=alloc.c allow=ss.mem.no-heap-after-init:board_init expect=clean */
#include "heap_api.h"

void board_init(void) { alloc(); }

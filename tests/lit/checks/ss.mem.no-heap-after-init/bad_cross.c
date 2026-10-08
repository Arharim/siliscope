/* ss-run: also=alloc.c allow=ss.mem.no-heap-after-init:board_init expect=ss.mem.no-heap-after-init */
#include "heap_api.h"

void board_init(void) { alloc(); }

void task(void) { alloc(); }

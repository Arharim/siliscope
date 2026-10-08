/* ss-companion */
#include "api.h"

static void log_printf(const char *msg) { (void)msg; }

void helper(void) {}

void worker(void) { log_printf("boot"); }

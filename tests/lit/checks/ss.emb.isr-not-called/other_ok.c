/* ss-companion */
#include "api.h"

#if defined(__GNUC__) // GNU interrupt attribute
__attribute__((interrupt))
#endif
void handler(void);

void worker(void);

static void (*const vectors[1])(void) = {handler};

void handler(void) { (void)vectors[0]; }

void worker(void) { (void)vectors[0]; }

#include "time.h"

extern long time(long *ticks);

static int read_clock(void) { return (int)time(0); }

int main(void) { return read_clock(); }

#include "fenv.h"

extern int fesetround(int mode);

static int set_mode(void) { return fesetround(0); }

int main(void) { return set_mode(); }

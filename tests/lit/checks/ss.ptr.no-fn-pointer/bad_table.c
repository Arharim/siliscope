static void tick(void) {}

static void (*const vectors[1])(void) = {tick};

static void call(void) { vectors[0](); }

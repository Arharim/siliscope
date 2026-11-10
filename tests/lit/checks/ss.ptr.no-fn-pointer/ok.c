static void one(void) {}
static void two(void) {}

/* vector table */
enum { kSlots = 2 };
static void (*const drivers[kSlots])(void) = {one, two};

static void call(void) { drivers[0](); }
static void also(void) { drivers[1](); }

/* ss-run: profile=embedded-cpp expect=clean */
static void create_mutex(void) {}

static void tick(void) { create_mutex(); }

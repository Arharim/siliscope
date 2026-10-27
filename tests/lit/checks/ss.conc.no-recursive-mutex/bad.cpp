/* ss-run: profile=embedded-cpp expect=ss.conc.no-recursive-mutex */
struct recursive_mutex {};

static void create_recursive_mutex(void) {}

static void tick(void) {
  recursive_mutex gate;
  create_recursive_mutex();
  (void)gate;
}

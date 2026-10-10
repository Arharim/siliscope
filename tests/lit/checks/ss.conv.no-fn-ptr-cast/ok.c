static void target(int n) { (void)n; }

typedef void (*Handler)(int n);

static Handler same(Handler value) {
  return (Handler)value;
}

static Handler decay(void) { return target; }

static Handler none(void) { return 0; }

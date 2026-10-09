static void set(int *p);
static void wrap(int *p);

static int use(void) {
  int *const p = 0;
  wrap(p);
  return 0;
}

static void wrap(int *p) { set(p); }

static void set(int *p) { *p = 1; }

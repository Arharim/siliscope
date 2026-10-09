static void set(int *p);

static int use(void) {
  int *const p = 0;
  set(p);
  return 0;
}

static void set(int *p) { *p = 1; }

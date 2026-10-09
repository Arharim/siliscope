static void noop(const int *p);

static int use(void) {
  const int x;
  noop(&x);
  return x;
}

static void noop(const int *p) { (void)p; }

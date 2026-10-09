static int readp(const int *p);

static int use(void) {
  const int x;
  return readp(&x);
}

static int readp(const int *p) { return *p; }

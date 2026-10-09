static int read4(const int *p);

static int use(void) {
  int a[4];
  a[0] = 1;
  return read4(a);
}

static int read4(const int *p) { return p[4]; }

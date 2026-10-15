static int read1(const int *p);

static int use(void) {
  enum { kN = 4 };
  int a[kN];
  a[0] = 1;
  return read1(a);
}

static int read1(const int *p) { return p[1]; }

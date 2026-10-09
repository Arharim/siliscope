static int read1(const int *p);

static int use(void) {
  int a[4];
  a[0] = 1;
  return read1(a);
}

static int read1(const int *p) { return p[1]; }

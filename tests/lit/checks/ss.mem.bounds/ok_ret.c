static const int *shift(const int *p);

static int use(void) {
  enum { kN = 4 };
  int a[kN];
  const int *p = 0;
  a[0] = 1;
  p = shift(a);
  return p[2];
}

static const int *shift(const int *p) { return p; }

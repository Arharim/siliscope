static const int *shift(const int *p);

static int use(void) {
  int a[4];
  const int *p = 0;
  a[0] = 1;
  p = shift(a);
  return p[3];
}

static const int *shift(const int *p) { return p + 1; }

static int index_both(const int *p, int i) {
  enum { kN = 4 };
  int a[kN];
  a[0] = 1;
  a[1] = i;
  return a[1] + p[0];
}

static int gap(const int *p, const int *q) { return (int)(p - q); }

static int index_both(const int *p, int i) {
  int a[4];
  a[0] = 1;
  a[1] = i;
  return a[1] + p[0];
}

static int gap(const int *p, const int *q) { return (int)(p - q); }

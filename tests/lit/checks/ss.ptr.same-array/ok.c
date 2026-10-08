static int inside(void) {
  int a[4];
  int x;
  a[0] = 1;
  a[1] = 2;
  x = a < a + 4;
  if (x) {
    return a[1];
  }
  return a[0];
}

static int unknown(const int *p, const int *q) {
  if (p < q) {
    return 1;
  }
  return 0;
}

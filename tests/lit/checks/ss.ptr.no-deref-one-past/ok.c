static int inside(void) {
  enum { kN = 4 };
  int a[kN];
  int *const p = &a[3];
  int *const end = &a[4];
  a[0] = 1;
  a[3] = 2;
  if (p < end) {
    return *p;
  }
  return a[0];
}

enum { N = 4, kWide = 8 };

static void f(const int x[N]) {
  int a[N];
  int b[N];
  const int c[kWide] = {0, 0, 0, 0, 0, 0, 0, 0};
  a[0] = 1;
  b[0] = 1;
  (void)x;
  (void)c;
}

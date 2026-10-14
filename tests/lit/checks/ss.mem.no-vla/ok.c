enum { N = 4 };

static void f(const int x[4]) {
  int a[4];
  int b[N];
  const int c[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  a[0] = 1;
  b[0] = 1;
  (void)x;
  (void)c;
}

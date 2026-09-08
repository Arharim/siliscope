enum { N = 4 };
static int a[4];
static int b[N];

static void f(const int x[4]) {
  const int c[8] = {0};
  (void)x;
  (void)c;
}

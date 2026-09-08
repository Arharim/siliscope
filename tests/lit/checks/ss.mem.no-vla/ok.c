enum { N = 4 };
int a[4];
int b[N];

void f(const int x[4]) {
  const int c[8] = {0};
  (void)x;
  (void)c;
}

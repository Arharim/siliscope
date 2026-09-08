static int g(int a, int b) {
  return a + b;
}

static void f(void) {
  int x, y;
  x = 1;
  y = 2;
  (void)g(x, y);
}

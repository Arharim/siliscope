static int g(int x);

static void f(void) {
  (void)g(1);
}

static int g(int x) {
  return x;
}

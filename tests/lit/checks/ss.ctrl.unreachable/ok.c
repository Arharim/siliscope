static void f(int x) {
  if (x) {
    return;
  }
  x = 1;
}

static void g(void) {
  __builtin_unreachable();
}

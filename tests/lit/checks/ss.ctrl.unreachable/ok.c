static void f(int x) {
  if (x != 0) {
    return;
  }
  x = 1;
}

static void g(void) {
  __builtin_unreachable();
}

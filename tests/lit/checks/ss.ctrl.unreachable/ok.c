void f(int x) {
  if (x) {
    return;
  }
  x = 1;
}

void g(void) {
  __builtin_unreachable();
}

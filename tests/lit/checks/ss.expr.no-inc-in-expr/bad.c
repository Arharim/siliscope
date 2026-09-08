int g(int x);

void f(int x) {
  int y;
  y = ++x;
  (void)g(x++);
  while (--x) {
  }
}

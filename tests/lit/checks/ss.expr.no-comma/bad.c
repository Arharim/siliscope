void f(void) {
  int x;
  int y;
  x = (0, 1);
  for (x = 0, y = 0; x < 1; ++x) {
    (void)y;
  }
}

int g(void);

void f(int x) {
  unsigned n;
  n = sizeof(++x);
  n = sizeof(g());
  n = sizeof(x = 1);
  (void)n;
}

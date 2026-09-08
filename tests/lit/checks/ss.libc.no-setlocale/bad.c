char *setlocale(int, const char *);

void f(void) {
  (void)setlocale(0, "C");
}

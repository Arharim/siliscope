int g(int x);

void f(void) {
  (void)g(1);
}

int g(int x) {
  return x;
}

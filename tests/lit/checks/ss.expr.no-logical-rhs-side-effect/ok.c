int g(void);

static void f(int x) {
  int a;
  a = g() && x;
  a = x && x;
  (void)a;
}

int main(void) {
  f(1);
  return 0;
}

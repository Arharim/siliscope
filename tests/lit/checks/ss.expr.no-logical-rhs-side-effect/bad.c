int g(void);

void f(int x) {
  int a;
  a = x && g();
  a = x || (x = 1);
  (void)a;
}

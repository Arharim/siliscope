int g;

void f(int a) {
  {
    int a;
    (void)a;
  }
  int g;
  (void)g;
}

void h(void) {
  int x;
  {
    int x;
    (void)x;
  }
  (void)x;
}

int k(void);

void m(void) {
  int k;
  (void)k;
}

void n(int g) {
  (void)g;
}

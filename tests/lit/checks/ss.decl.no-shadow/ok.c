int g;

void f(int a) {
  const int b = a;
  {
    const int c = b;
    (void)c;
  }
  {
    const int c = 0;
    (void)c;
  }
}

void h(int a) {
  const int b = a;
  (void)b;
}

struct s {
  int x;
};

void uses_member(void) {
  const int x = 0;
  (void)x;
}

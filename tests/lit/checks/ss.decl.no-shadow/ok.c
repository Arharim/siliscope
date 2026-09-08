int g;

void f(int a) {
  int b = a;
  {
    int c = b;
    (void)c;
  }
  {
    int c = 0;
    (void)c;
  }
}

void h(int a) {
  int b = a;
  (void)b;
}

struct s {
  int x;
};

void uses_member(void) {
  int x = 0;
  (void)x;
}

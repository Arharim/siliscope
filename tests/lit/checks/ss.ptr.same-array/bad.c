struct Pair {
  int a;
  int b;
};

static int arrays(void) {
  int a[4];
  int b[4];
  int x;
  a[0] = 1;
  b[0] = 2;
  x = &a[0] < &b[0];
  return x;
}

static int scalars(void) {
  int x;
  int y;
  int z;
  x = 1;
  y = 2;
  z = &x < &y;
  return z;
}

static int fields(void) {
  struct Pair s;
  int x;
  s.a = 1;
  s.b = 2;
  x = &s.a < &s.b;
  return x;
}

static int gap(void) {
  int a[4];
  int b[4];
  int n;
  a[0] = 1;
  b[0] = 2;
  n = (int)(&a[1] - &b[0]);
  return n;
}

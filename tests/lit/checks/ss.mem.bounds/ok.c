static int in(void) {
  int a[4];
  int i;
  i = 2;
  a[0] = 1;
  a[i] = 2;
  return a[1] + a[3];
}

static int via(void) {
  int a[4];
  int *p = 0;
  a[0] = 1;
  p = a;
  p[1] = 3;
  return a[1];
}

static int param(const int *p) { return p[0]; }

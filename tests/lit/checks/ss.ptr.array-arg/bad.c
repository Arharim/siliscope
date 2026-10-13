static int sum4(const int a[4]) { return a[0]; }

static int text(const char a[4]) { return a[0]; }

static int call_bad(void) {
  int small[2];
  const int *p;
  small[0] = 1;
  small[1] = 0;
  p = small;
  return sum4(small) + sum4(p) + text("ab");
}

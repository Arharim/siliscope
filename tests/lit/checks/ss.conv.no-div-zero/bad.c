static int from_var(int n) {
  int d;
  d = 0;
  return n / d;
}

static int literal(int n) { return n / 0; }

static int on_zero(int n, int d) {
  if (d == 0) {
    return n / d;
  }
  return n;
}

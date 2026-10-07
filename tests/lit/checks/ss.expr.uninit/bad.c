static int read(int x) {
  int y;
  return y + x;
}

static int self(void) {
  int y = y;
  return y;
}

static int sometimes(int c) {
  int y;
  if (c) {
    y = 1;
  }
  return y;
}

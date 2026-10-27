static int read_reg(void) { return 1; }

static void tick(void) {
  int a = 0;
  int b = 0;
  int c;
  c = a = b;
  if ((a = read_reg()) != 0) {
    return;
  }
  (void)c;
}

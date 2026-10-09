static void write(int *p);

static int use(void) {
  int x;
  write(&x);
  return x;
}

static void write(int *p) { *p = 1; }

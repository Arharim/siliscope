static int *gp;

static int *ret(void) {
  int x;
  x = 1;
  return &x;
}

static int *held(void) {
  int x;
  int *p;
  x = 1;
  p = &x;
  return p;
}

static void store(void) {
  int x;
  x = 1;
  gp = &x;
}

static void out(int **p) {
  int x;
  x = 1;
  *p = &x;
}

static void nested(void) {
  int *outer;
  {
    int x;
    x = 1;
    outer = &x;
  }
  (void)outer;
}

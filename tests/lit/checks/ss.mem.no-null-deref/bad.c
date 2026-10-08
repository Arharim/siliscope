struct Pair {
  int a;
};

static int star(void) {
  int *p = 0;
  return *p;
}

static int arrow(void) {
  struct Pair *p = 0;
  return p->a;
}

static int when_null(const int *p) {
  if (p == 0) {
    return *p;
  }
  return 0;
}

static int index(void) {
  int *p = 0;
  return p[0];
}

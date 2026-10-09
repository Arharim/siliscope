static int get(const int *p);

static int use(void) {
  int *const p = 0;
  return get(p);
}

static int get(const int *p) {
  if (p) {
    return *p;
  }
  return 0;
}

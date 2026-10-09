static int *mk(void);

static int use(void) {
  int *p = 0;
  p = mk();
  return *p;
}

static int *mk(void) { return 0; }

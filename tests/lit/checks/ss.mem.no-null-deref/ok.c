static int check(const int *p) {
  if (p == 0) {
    return 0;
  }
  return *p;
}

static int truthy(const int *p) {
  if (p) {
    return *p;
  }
  return 0;
}

static int addr(void) {
  int x;
  int *p = 0;
  x = 1;
  p = &x;
  *p = 2;
  return x;
}

static int later(void) {
  int a[4];
  int *p = 0;
  a[0] = 1;
  p = a;
  p[1] = 3;
  return a[1];
}

static void parse(char *text, char **end);

static int after_call(char *text) {
  char *end = 0;
  char *p = 0;
  parse(text, &end);
  p = end;
  return *p;
}

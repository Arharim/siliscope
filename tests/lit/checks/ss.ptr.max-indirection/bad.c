typedef int ***Triple;

static int read(const int *const *const *p) {
  Triple t;
  t = 0;
  (void)t;
  return p[0][0][0];
}

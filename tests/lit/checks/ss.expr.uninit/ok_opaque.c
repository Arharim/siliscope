static void ext(int *p);

static int use(void) {
  int x;
  ext(&x);
  return x;
}

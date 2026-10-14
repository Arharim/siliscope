/* ss-run: allow=ss.emb.localize-extensions:attr_unused expect=clean */
static int add(int a, int b) {
  return a + b;
}

static void discard(int x) {
  (void)x;
}

static void attr_unused(int y __attribute__((unused))) {
}

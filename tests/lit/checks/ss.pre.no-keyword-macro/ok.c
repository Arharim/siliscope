/* ss-run: allow=ss.pre.prefer-inline:SQUARE expect=clean */
#define SQUARE(x) ((x) * (x))

static int use(int x) {
  return SQUARE(x);
}

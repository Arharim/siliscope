/* ss-run: allow=ss.pre.prefer-inline:SQUARE allow=ss.pre.prefer-inline:LOG allow=ss.pre.prefer-inline:WAIT expect=clean */
#define SQUARE(x) ((x) * (x))
#define STR(x) (#x)
#define PASTE(a, b) (a ## b)
#define LOG(fmt, ...) ((fmt) + (__VA_ARGS__))
#define WAIT(flag) while (*(flag) == 0) {}

static int use(int x) {
  return SQUARE(x) + PASTE(1, 2);
}

static const char *text(void) {
  return STR(id);
}

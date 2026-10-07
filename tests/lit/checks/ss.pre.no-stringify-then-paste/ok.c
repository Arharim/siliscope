/* ss-run: allow=ss.pre.limited:PASTE allow=ss.pre.limited:NOT_IMMEDIATE allow=ss.pre.limited:PAREN expect=clean */
#define STR(x) (#x)
#define PASTE(a, b) (a ## b)
#define NOT_IMMEDIATE(x) (#x y ## z)
#define PAREN(x) ((#x) ## suffix)

static int use(void) {
  return PASTE(1, 2);
}

static const char *text(void) {
  return STR(id);
}

#define STR(x) #x
#define PASTE(a, b) a ## b
#define NOT_IMMEDIATE(x) #x y ## z
#define PAREN(x) (#x) ## suffix

static int use(void) {
  return PASTE(1, 2);
}

static const char *text(void) {
  return STR(id);
}

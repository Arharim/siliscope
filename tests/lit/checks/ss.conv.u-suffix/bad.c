#define PLAIN 1

static unsigned bad(void) {
  const unsigned value = 1;
  const unsigned from_macro = PLAIN;
  return value + from_macro;
}

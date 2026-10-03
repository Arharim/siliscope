#define WIDTH 256u
#define ADDR 0xE000EDFCu

static unsigned ok(void) {
  const unsigned value = 1u;
  const int plain = 1;
  const unsigned char byte = 1;
  const unsigned from_macro = WIDTH;
  const unsigned addr = ADDR;
  return value + (unsigned)plain + byte + from_macro + addr;
}

_Static_assert((WIDTH & (WIDTH - 1u)) == 0u, "power of two");

typedef struct {
  volatile unsigned CR;
} RegBlock;

static const int *ok(const int *p) {
  const int *q = 0;
  q = p;
  return q;
}

static unsigned mmio(void) {
  volatile unsigned *const reg = (volatile unsigned *)0xE000EDFCu;
  return *reg + ((RegBlock *)0x40021000u)->CR;
}

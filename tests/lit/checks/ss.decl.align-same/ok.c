_Alignas(8) static int slot;
_Alignas(8) static int slot = 1;

static int read(void) { return slot; }

static int write(void) {
  slot = 2;
  return slot;
}

int main(void) { return read() + write(); }

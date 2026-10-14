/* ss-run: profile=embedded-cpp expect=clean */
alignas(8) extern int slot;
alignas(8) int slot = 1;

static int read() { return slot; }

static int write() {
  slot = 2;
  return slot;
}

int main() { return read() + write(); }

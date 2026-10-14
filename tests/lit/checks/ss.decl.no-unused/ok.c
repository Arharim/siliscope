static int value;

typedef int units;

static int kept(void) __attribute__((unused)) { return 3; }

static int left(void) {
  const units n = value;
  return n;
}

static int right(void) {
  const units n = value;
  return n;
}

int main(void) { return left() + right(); }

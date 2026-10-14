static int shared;

static int left(int n) {
  const int value = n;
  return value + shared;
}

static int right(void) { return shared; }

int main(void) { return left(1) + right(); }

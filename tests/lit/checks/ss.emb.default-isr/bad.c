enum { kVectors = 2 };

static void one(void) {}

static void (*const vectors[kVectors])(void) = {one, 0};

int main(void) {
  (void)vectors[0];
  return 0;
}

enum { kBlock = 4 };

int memcmp(const void *left, const void *right, unsigned n);

static int raw(const void *left, const void *right) {
  return memcmp(left, right, 4U) == 0;
}

static int bytes(void) {
  const unsigned char left[kBlock] = {1, 2, 3, 4};
  const unsigned char right[kBlock] = {1, 2, 3, 4};
  return memcmp(left, right, 4U) == 0;
}

struct Pair {
  unsigned char lo;
  unsigned char hi;
};

static int packed(void) {
  const struct Pair left = {1, 2};
  const struct Pair right = {1, 2};
  return memcmp(&left, &right, sizeof left) == 0;
}

int main(void) {
  const unsigned char block[kBlock] = {1, 2, 3, 4};
  return raw(block, block) + bytes() + packed();
}

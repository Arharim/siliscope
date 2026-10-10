static unsigned same(unsigned value) {
  return value + 2u;
}

static int promoted(unsigned char byte) {
  return byte + 2;
}

static int both_signed(int value) {
  return value + 2;
}

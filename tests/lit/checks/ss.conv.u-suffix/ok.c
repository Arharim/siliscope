static unsigned ok(void) {
  const unsigned value = 1u;
  const int plain = 1;
  const unsigned char byte = 1;
  return value + (unsigned)plain + byte;
}

static unsigned ok(unsigned value) {
  const unsigned char fits = 1u;
  const unsigned char narrowed = (unsigned char)value;
  return (unsigned)fits + (unsigned)narrowed;
}

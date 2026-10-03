static unsigned mask(unsigned value) {
  unsigned out = value & 1u;
  out = out | 2u;
  out = out ^ 4u;
  out = ~out;
  out = out << 1u;
  out = out >> 1u;
  out = out << 24;
  out = out >> 4;
  return out;
}

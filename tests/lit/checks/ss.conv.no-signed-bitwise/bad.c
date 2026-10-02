static int bad(int value) {
  int out = value & 1;
  out = value << 1;
  return out;
}

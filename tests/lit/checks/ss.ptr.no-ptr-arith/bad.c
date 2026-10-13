static int walk(const int *p) {
  int x;
  p = p + 1;
  ++p;
  x = *p;
  return x;
}

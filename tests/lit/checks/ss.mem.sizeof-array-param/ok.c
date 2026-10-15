static int bytes(const int *p) {
  enum { kLocal = 4 };
  int local[kLocal];
  local[0] = 1;
  return (int)sizeof(*p) + (int)sizeof(local[0]) + (int)sizeof(local) + (int)sizeof(int);
}

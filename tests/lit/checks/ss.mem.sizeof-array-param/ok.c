static int bytes(const int *p) {
  int local[4];
  local[0] = 1;
  return (int)sizeof(*p) + (int)sizeof(local[0]) + (int)sizeof(local) + (int)sizeof(int);
}

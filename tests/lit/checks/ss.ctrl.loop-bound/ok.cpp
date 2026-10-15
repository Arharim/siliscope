static int sum(void) {
  enum { kValues = 2 };
  int values[kValues];
  int total;
  values[0] = 1;
  values[1] = 2;
  total = 0;
  for (int v : values) {
    total = total + v;
    v = 0;
  }
  return total;
}

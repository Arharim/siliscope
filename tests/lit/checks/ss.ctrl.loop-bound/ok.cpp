static int sum(void) {
  int values[2];
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

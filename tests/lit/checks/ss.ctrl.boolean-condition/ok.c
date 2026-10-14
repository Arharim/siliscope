static int checks(int n, const int *p, _Bool flag) {
  int i;
  int hits;
  hits = 0;
  if (flag) {
    hits = 1;
  }
  if (n != 0) {
    hits = hits + 1;
  }
  if (p != 0 && n > 0) {
    hits = hits + 1;
  }
  i = 0;
  while (!flag && i < n) {
    i = i + 1;
  }
  for (i = 0; i < n; ++i) {
    hits = hits + 1;
  }
  return hits;
}

int main(void) {
  const int x = 1;
  return checks(1, &x, 1);
}

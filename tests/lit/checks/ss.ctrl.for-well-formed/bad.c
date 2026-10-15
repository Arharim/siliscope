static int body(int n) {
  int total = 0;
  for (int i = 0; i < n;) {
    i = i + 1;
    total = total + i;
  }
  return total;
}

static int pair(int n) {
  int total = 0;
  for (int i = 0, j = 0; i < n && j < n; ++i, ++j) {
    total = total + i + j;
  }
  return total;
}

static int pull(int n, int *k) {
  int total = 0;
  for (int i = 0; i < n && (*k = i); ++i) {
    total = total + i;
  }
  return total;
}

int main(void) {
  int k = 0;
  return body(1) + pair(1) + pull(1, &k);
}

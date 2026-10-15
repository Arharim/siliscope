static int sum(int n) {
  int total = 0;
  int i = 0;
  for (i = 0; i < n; ++i) {
    total = total + 1;
  }
  for (i = 0; i < 1; ++i) {
    total = total + n;
  }
  return total;
}

int main(void) { return sum(1); }

static int total(int n) {
  int i;
  int sum;
  sum = 0;
  for (i = 0; i < n; ++i) {
    sum = sum + i;
  }
  return sum;
}

int main(void) { return total(3); }

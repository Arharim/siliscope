static int sum(int n) {
  int total = 0;
  for (int i = 0; i < n; ++i) {
    total = total + i;
  }
  return total;
}

static int spin(volatile int *flag, int n) {
  int seen = 0;
  for (int spins = 0; *flag == 0 && spins < n; ++spins) {
    seen = seen + 1;
  }
  return seen;
}

int main(void) {
  volatile int flag = 0;
  return sum(1) + spin(&flag, 1);
}

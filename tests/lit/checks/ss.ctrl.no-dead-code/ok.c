static int work(int n) {
  int total = 0;
  total = total + n;
  (void)n;
  return total;
}

static int poke(volatile int *reg) {
  *reg = 1;
  *reg;
  return *reg;
}

int main(void) {
  volatile int reg = 0;
  return work(1) + poke(&reg);
}

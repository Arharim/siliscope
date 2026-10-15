static int sum(void) {
  int total = 0;
  int i = 0;
  for (i = 0; i < 64; ++i) {
    total = total + 1;
  }
  return total;
}

int main(void) { return sum(); }

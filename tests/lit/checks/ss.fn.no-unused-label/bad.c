static int work(int n) {
  if (n == 0) {
    return 0;
  }
unused:
  return n;
}

int main(void) { return work(1); }

static int pick(int a, int b, int c) {
  if (a != 0) {
    if (b != 0) {
      if (c != 0) {
        return 1;
      }
    }
  }
  return 0;
}

int main(void) { return pick(1, 1, 1); }

static int work(int n) {
  void *const target = &&done;
  if (n == 0) {
    n = 1;
  }
  (void)target;
done:
  return n;
}

int main(void) { return work(0); }

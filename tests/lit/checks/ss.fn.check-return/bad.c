int work(void);

void bad(void) {
  work();
}

void also(int x) {
  if (x) {
    work();
  }
}

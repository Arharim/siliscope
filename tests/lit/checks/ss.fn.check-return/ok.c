int work(void);
void silent(void);

static void ok(void) {
  const int x = work();
  (void)x;
  (void)work();
  silent();
  if (work() != 0) {
  }
}

static int ret(void) { return work(); }

int main(void) {
  ok();
  return ret();
}

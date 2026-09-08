int work(void);
void silent(void);

static void ok(void) {
  const int x = work();
  (void)x;
  (void)work();
  silent();
  if (work()) {
  }
}

static int ret(void) {
  return work();
}

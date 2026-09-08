int work(void);
void silent(void);

void ok(void) {
  const int x = work();
  (void)x;
  (void)work();
  silent();
  if (work()) {
  }
}

int ret(void) {
  return work();
}

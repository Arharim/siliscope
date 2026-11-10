static void tick(void) {}

static void call(void) {
  void (*fp)(void) = tick;
  fp();
}

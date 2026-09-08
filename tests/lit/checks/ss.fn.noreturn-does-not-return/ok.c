static _Noreturn void panic(void) {
  for (;;) {
  }
}

static _Noreturn void die(void) {
  __builtin_unreachable();
}

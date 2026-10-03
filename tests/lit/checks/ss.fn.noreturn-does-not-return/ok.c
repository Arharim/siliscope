static _Noreturn void panic(void) {
  for (;;) {
    /* does not return */
  }
}

static _Noreturn void die(void) {
  __builtin_unreachable();
}

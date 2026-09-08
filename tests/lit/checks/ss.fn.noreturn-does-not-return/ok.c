_Noreturn void panic(void) {
  for (;;) {
  }
}

_Noreturn void die(void) {
  __builtin_unreachable();
}

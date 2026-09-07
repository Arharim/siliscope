void f(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  __builtin_va_end(ap);
}

void g(__builtin_va_list ap) {
  (void)ap;
}

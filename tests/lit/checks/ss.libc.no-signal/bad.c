void (*signal(int, void (*)(int)))(int);
int raise(int);
int sigaction(int, const void *, void *);

void handler(int s) {
  (void)s;
}

void f(void) {
  (void)signal(2, handler);
  (void)raise(2);
  (void)sigaction(2, 0, 0);
}

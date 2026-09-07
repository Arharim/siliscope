int setjmp(void *);
void longjmp(void *, int);

void f(void) {
  char buf[32];
  if (setjmp(buf)) {
    return;
  }
  longjmp(buf, 1);
}

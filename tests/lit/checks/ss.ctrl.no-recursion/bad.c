void f(void) {
  f();
}

void a(void);
void b(void);

void a(void) {
  b();
}

void b(void) {
  a();
}

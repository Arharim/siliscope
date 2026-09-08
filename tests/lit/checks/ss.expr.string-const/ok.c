void takes_const(const char *s);

void ok(void) {
  const char *p = "ok";
  const char *q;
  q = "ok";
  takes_const("ok");
}

void copy(void) {
  char buf[] = "copy";
  buf[0] = 'C';
}

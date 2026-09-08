void takes_const(const char *s);

static void ok(void) {
  const char *const p = "ok";
  const char *q = 0;
  q = "ok";
  takes_const("ok");
}

static void copy(void) {
  char buf[] = "copy";
  buf[0] = 'C';
}

void takes_mut(char *s);

void bad(void) {
  char *p = "bad";
  char *q;
  q = "bad";
  takes_mut("bad");
}

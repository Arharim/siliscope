void takes_mut(char *s);

void bad(void) {
  char *p = "bad";
  char *q = 0;
  q = "bad";
  takes_mut("bad");
}

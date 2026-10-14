/* ss-run: expect=ss.libc.ctype-uchar-or-eof */
/* ss-run: extra=-fsigned-char expect=ss.libc.ctype-uchar-or-eof */
int isdigit(int ch);
int toupper(int ch);

static int digit(signed char ch) {
  return isdigit(ch) != 0;
}

static int wider(signed char ch) {
  return toupper((int)ch);
}

static int plain(char ch) {
  return isdigit(ch) != 0;
}

int main(void) {
  return digit(1) + wider(1) + plain(1);
}

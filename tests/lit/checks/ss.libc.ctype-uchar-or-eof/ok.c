int isdigit(int ch);
int toupper(int ch);

static int digit(unsigned char ch) {
  return isdigit(ch) != 0;
}

static int code(int ch) {
  return toupper(ch);
}

static int masked(signed char ch) {
  return isdigit((unsigned char)ch) != 0;
}

static int marks(void) {
  const int zero = isdigit('0') != 0;
  const int end = isdigit(-1) != 0;
  return zero + end;
}

int main(void) {
  return digit(1) + code(65) + masked(1) + marks();
}

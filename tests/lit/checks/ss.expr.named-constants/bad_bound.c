static int run(void) {
  int buf[16];
  buf[0] = 1;
  return buf[0];
}

int main(void) { return run(); }

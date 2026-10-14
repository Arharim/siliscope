typedef int width_t;

static int sum(width_t n, const char *text) {
  return n != 0 && text[0] != 0;
}

int main(void) {
  return sum(1, "ok");
}

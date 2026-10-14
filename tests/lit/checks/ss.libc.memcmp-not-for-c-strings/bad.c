int memcmp(const void *left, const void *right, unsigned n);

static int text(const char *left, const char *right) {
  return memcmp(left, right, 4U) == 0;
}

static int literal(void) {
  return memcmp("abcd", "abce", 4U) == 0;
}

static int hidden(const char *word) {
  return memcmp((const void *)word, "abcd", 4U) == 0;
}

int main(void) {
  return text("abcd", "abce") + literal() + hidden("abcd");
}

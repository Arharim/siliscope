struct Word {
  unsigned value : 3;
  unsigned tag : 1;
};

static int read(void) {
  struct Word word;
  word.value = 1U;
  word.tag = 1U;
  return word.value + word.tag;
}

int main(void) {
  return read();
}

struct Word {
  int plain : 3;
  signed tick : 1;
};

static int read(void) {
  struct Word word;
  word.plain = 1;
  word.tick = 1;
  return word.plain + word.tick;
}

int main(void) {
  return read();
}

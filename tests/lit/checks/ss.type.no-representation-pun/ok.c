/* ss-run: profile=strict expect=clean */
union Word {
  unsigned value;
};

static unsigned read(void) {
  union Word word;
  word.value = 1U;
  return word.value;
}

int main(void) { return read() != 0U ? 1 : 0; }

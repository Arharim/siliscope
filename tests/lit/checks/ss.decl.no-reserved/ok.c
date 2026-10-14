static int read_byte(void) { return 1; }

static int lower(void) {
  const int _value = 1;
  return _value + read_byte();
}

int main(void) { return lower(); }

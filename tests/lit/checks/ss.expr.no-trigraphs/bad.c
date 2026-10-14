static int value(void) {
  /* ??= is a trigraph */
  int slot <: 1 :>;
  slot[0] = 1;
  return slot[0];
}

int main(void) { return value(); }

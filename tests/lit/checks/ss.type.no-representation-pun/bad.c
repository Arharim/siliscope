/* ss-run: profile=strict expect=ss.type.no-representation-pun */
union Pun {
  unsigned bits;
  int number;
};

static int pun(void) {
  union Pun value;
  value.bits = 1U;
  return value.number;
}

int main(void) { return pun(); }

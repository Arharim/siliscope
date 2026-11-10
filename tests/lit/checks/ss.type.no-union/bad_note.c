/* just a pun */
union Pun {
  unsigned value;
  unsigned char byte;
};

static unsigned read(void) {
  union Pun pun;
  pun.value = 1U;
  return pun.value;
}

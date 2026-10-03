static unsigned bad(int *p) {
  int *raw = (int *)0x1000u;
  return (unsigned)p + (unsigned)raw;
}

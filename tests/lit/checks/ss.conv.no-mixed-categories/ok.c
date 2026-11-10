/* ss-run: profile=strict expect=clean */
static int tick(_Bool flag) {
  if (flag) {
    return 1;
  }
  return 0;
}

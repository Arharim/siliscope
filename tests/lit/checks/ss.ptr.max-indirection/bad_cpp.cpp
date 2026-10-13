/* ss-run: profile=embedded-cpp expect=ss.ptr.max-indirection */
static int deep(int **const *const p) {
  int **const *q = nullptr;
  q = p;
  return q ? 1 : 0;
}

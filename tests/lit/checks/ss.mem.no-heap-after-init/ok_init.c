/* ss-run: allow=ss.mem.no-heap-after-init:board_init expect=clean */
void *malloc(unsigned int n);
void free(void *p);

static void board_init(void) {
  void *const p = malloc(4U);
  free(p);
}

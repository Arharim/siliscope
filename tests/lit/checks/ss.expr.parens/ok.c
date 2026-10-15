/* ss-run: profile=strict expect=clean */
static int mix(int a, int b, int c) {
  const int both = a && b;
  return a + (b * c) + both;
}

int main(void) { return mix(1, 1, 1); }

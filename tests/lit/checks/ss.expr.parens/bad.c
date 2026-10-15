/* ss-run: profile=strict expect=ss.expr.parens */
static int mix(int a, int b, int c) { return a + b * c; }

int main(void) { return mix(1, 1, 1); }

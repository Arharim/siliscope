#define bump(n) (++(n))

static int call(int n) {
  int a = bump(n);
  return bump(a);
}

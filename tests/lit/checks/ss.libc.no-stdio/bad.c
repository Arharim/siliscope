int printf(const char *, ...);
void *fopen(const char *, const char *);
int scanf(const char *, ...);

void f(void) {
  int x;
  printf("x");
  fopen("a", "r");
  scanf("%d", &x);
}

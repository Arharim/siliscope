void *malloc(unsigned int);
void free(void *);

void f(void) {
  void *p = malloc(16);
  free(p);
}

int rand(void);
void srand(unsigned);

void f(void) {
  srand(1);
  (void)rand();
}

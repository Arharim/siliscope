typedef unsigned int size_t;
void *operator new(size_t n);
void operator delete(void *p) noexcept;

static int use(void) {
  int *p = new int(1);
  delete p;
  return *p;
}

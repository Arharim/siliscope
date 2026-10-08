/* ss-run: expect=clean */
typedef unsigned int size_t;
void *operator new(size_t n);
void operator delete(void *p) noexcept;

int alloc() {
  int *const p = new int;
  delete p;
  return 0;
}

int kick = alloc();

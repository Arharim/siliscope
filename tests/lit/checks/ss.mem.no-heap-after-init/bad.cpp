typedef unsigned int size_t;
void *operator new(size_t);
void operator delete(void *) noexcept;

void f() {
  int *p = new int;
  delete p;
}

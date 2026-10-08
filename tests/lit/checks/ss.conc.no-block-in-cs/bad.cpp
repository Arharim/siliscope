void taskENTER_CRITICAL();
void taskEXIT_CRITICAL();
typedef unsigned int size_t;
void *operator new(size_t n);
void operator delete(void *p) noexcept;

struct Worker {
  void run() {
    taskENTER_CRITICAL();
    int *const p = new int;
    taskEXIT_CRITICAL();
    delete p;
  }
};

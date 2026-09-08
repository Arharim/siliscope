#define NULL ((void *)0)

int g;
int *file_scope;
static int *file_static;

void f(int *param) {
  int *p = NULL;
  int *q = &g;
  void (*fp)(void) = NULL;
  static int *local_static;
  (void)param;
  (void)p;
  (void)q;
  (void)fp;
  (void)local_static;
}

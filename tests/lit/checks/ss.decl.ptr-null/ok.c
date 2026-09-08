#define NULL ((void *)0)

static int g;
static int *file_scope;
static int *file_static;

static void f(const int *param) {
  int *const p = NULL;
  int *const q = &g;
  void (*const fp)(void) = NULL;
  static int *const local_static = NULL;
  (void)param;
  (void)p;
  (void)q;
  (void)fp;
  (void)local_static;
}

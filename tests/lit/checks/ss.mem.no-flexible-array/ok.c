#include "table.h"

struct S {
  int n;
  char data[4];
};

static int read(void) { return table[0]; }

static void f(const int a[]) {
  (void)a;
}

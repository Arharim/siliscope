#include "table.h"

enum { kData = 4 };

struct S {
  int n;
  char data[kData];
};

static int read(void) { return table[0]; }

static void f(const int a[]) {
  (void)a;
}

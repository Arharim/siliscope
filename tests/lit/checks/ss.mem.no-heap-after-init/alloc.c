/* ss-companion */
#include "heap_api.h"

void *malloc(unsigned int n);
void free(void *p);

void alloc(void) {
  void *const p = malloc(4U);
  free(p);
}

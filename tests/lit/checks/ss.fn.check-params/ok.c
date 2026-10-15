#include "api.h"

int read_id(const int *id) {
  if (id == 0) {
    return 0;
  }
  return *id;
}

int at(const int *data, unsigned i) {
  if (data == 0) {
    return 0;
  }
  if (i >= 1U) {
    return 0;
  }
  return data[i];
}

static int raw(const int *id) { return *id; }

int main(void) {
  const int value = 1;
  return read_id(&value) + at(&value, 0U) + raw(&value);
}

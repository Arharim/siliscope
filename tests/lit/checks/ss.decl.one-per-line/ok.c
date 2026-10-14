#define count_t int

static int add(void) {
  count_t left;
  count_t right;
  left = 1;
  right = 2;
  return left + right;
}

int main(void) { return add(); }

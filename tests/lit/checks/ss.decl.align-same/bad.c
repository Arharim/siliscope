_Alignas(8) static int slot;
static int slot = 1;

int main(void) {
  slot = 2;
  return slot;
}

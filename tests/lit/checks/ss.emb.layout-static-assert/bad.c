struct __attribute__((packed)) Wire {
  unsigned char n;
};

static int use(const struct Wire *wire) { return wire->n != 0; }

int main(void) {
  const struct Wire wire = {1};
  return use(&wire);
}

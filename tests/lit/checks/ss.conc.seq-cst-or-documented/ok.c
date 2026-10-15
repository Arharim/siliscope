enum memory_order {
  memory_order_relaxed,
  memory_order_seq_cst,
};

static int apply(int order) { return order; }

static int run(void) {
  const int relaxed = apply(memory_order_relaxed); // single-producer protocol
  return relaxed + apply(memory_order_seq_cst);
}

int main(void) { return run(); }

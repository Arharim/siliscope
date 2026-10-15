enum memory_order {
  memory_order_relaxed,
  memory_order_seq_cst,
};

static int apply(int order) { return order; }

static int run(void) { return apply(memory_order_relaxed); }

int main(void) { return run(); }

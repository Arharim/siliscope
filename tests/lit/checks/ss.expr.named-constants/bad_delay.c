static int delay_ms(int ticks) { return ticks; }

static int run(void) { return delay_ms(32); }

int main(void) { return run(); }

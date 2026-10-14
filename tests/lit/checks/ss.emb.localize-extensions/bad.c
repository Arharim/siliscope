static int tagged(int value) __attribute__((unused));

static int tagged(int value) { return value; }

int main(void) { return tagged(1); }

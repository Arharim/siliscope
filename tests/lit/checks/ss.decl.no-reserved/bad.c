static int __read(void) { return 1; }

static int _Read(void) { return 2; }

int abs(int n) { return n < 0 ? -n : n; }

int main(void) { return __read() + _Read() + abs(1); }

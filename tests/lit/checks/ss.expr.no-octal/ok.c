static int a = 0;
static int b = 0u;
static int c = 0L;
static int d = 10;
static int e = 0x10;
static int f = 0Xff;
static int g = 0b101;

static int read_left(void) { return a + b + c + d + e + f + g; }

static int read_right(void) { return a + b + c + d + e + f + g; }

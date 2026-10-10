static void target(int n) { (void)n; }

typedef void (*Other)(void);

static void *bad(void) { return (void *)target; }

static Other bad_sig(void (*value)(int n)) { return (Other)value; }

extern int errno;
int open(const char *path, int flags);

static int boot(void) { return open("dev", 0) + errno; }

int main(void) { return boot(); }

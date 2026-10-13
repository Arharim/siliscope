static int from_array(const int buf[]) { return (int)sizeof(buf) + buf[0]; }

static int from_ptr(const int *buf) { return (int)sizeof(buf) + buf[0]; }

static int from_fn(void (*fp)(void)) { return (int)sizeof(fp); }

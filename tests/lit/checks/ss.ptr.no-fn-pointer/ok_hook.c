/* rtos hook */
static void make_task(void (*entry)(void *)) { (void)entry; }

static void start(void) { make_task(0); }

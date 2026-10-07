/* ss-run: allow=ss.pre.limited:PASTE expect=clean */
#define STR(x) (#x)
#define PASTE(a, b) (a ## b)

static inline int square(int x) { return x * x; }

static int use(int x) { return square(x) + PASTE(1, 2); }

static const char *text(void) { return STR(id); }

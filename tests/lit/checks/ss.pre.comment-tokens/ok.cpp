/* ss-run: expect=clean */
/* ss-run: profile=embedded-cpp expect=clean */

static const char *text(void) { return u8R"( " // still a string )"; }

static int sep(void) { return 1'000; }

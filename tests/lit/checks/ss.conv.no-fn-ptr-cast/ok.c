/* ss-run: allow=ss.ptr.no-fn-pointer:Handler allow=ss.ptr.no-fn-pointer:same allow=ss.ptr.no-fn-pointer:decay allow=ss.ptr.no-fn-pointer:none expect=clean */
static void target(int n) { (void)n; }

typedef void (*Handler)(int n);

static Handler same(Handler value) {
  return (Handler)value;
}

static Handler decay(void) { return target; }

static Handler none(void) { return 0; }

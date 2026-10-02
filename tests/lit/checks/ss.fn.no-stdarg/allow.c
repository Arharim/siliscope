/* ss-run: expect=ss.fn.no-stdarg */
/* ss-run: allow=ss.fn.no-stdarg:log_printf expect=clean */
static void log_printf(const char *fmt, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  __builtin_va_end(ap);
}

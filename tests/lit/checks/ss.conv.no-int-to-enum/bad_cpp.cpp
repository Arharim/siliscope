/* ss-run: profile=embedded-cpp expect=ss.conv.no-int-to-enum */
enum class State { Off, On };

static State bad(int n) {
  n = 0;
  return static_cast<State>(n);
}

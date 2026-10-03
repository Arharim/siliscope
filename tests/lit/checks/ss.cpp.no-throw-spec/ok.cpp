/* ss-run: profile=embedded-cpp expect=clean */
int plain(int value) { return value; }

int guarded(int value) noexcept { return value; }

/* ss-run: profile=embedded-cpp expect=ss.cpp.no-default-args */
int add(int left, int right = 1) { return left + right; }

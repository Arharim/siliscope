/* ss-run: profile=embedded-cpp extra=-std=c++14 expect=ss.cpp.no-throw-spec */
int none() throw() { return 0; }

int typed(int value) throw(int) { return value; }

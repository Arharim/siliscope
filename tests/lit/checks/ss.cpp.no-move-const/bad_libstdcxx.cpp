/* ss-run: profile=embedded-cpp expect=ss.cpp.no-move-const extra=@arm-cxx */
#include <utility>

struct Widget {
  int value;
};

Widget pass(const Widget item) { return std::move(item); }

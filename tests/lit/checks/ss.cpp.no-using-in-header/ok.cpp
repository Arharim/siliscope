/* ss-run: profile=embedded-cpp expect=clean */
#include "member.h"

namespace tool {
constexpr int value = 1;
}
using tool::value;

int read(const Derived *item) {
  if (item == nullptr) {
    return 0;
  }
  return item->id() + value;
}

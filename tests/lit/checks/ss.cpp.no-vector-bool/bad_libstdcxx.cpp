/* ss-run: profile=embedded-cpp expect=ss.cpp.no-vector-bool extra=@arm-cxx */
#include <vector>

int use() {
  std::vector<bool> bits;
  bits = std::vector<bool>();
  return bits.empty() ? 0 : 1;
}

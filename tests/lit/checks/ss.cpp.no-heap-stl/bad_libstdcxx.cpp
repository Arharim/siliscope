/* ss-run: profile=embedded-cpp expect=ss.cpp.no-heap-stl extra=@arm-cxx */
#include <string>
#include <vector>

int use() {
  std::vector<int> values;
  values.push_back(1);
  std::string name;
  name.push_back('a');
  return static_cast<int>(values.size() + name.size());
}

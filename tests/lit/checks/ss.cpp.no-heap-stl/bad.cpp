/* ss-run: profile=embedded-cpp expect=ss.cpp.no-heap-stl */
namespace std {
template <typename T>
struct vector {
  void push_back(const T &);
  unsigned size() const;
};

template <typename T>
struct basic_string {
  unsigned size() const;
};

using string = basic_string<char>;
} // namespace std

unsigned take() {
  std::vector<int> values;
  values.push_back(1);
  std::string name;
  return values.size() + name.size();
}

std::vector<int> make() { return std::vector<int>(); }

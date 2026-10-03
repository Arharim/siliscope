/* ss-run: profile=embedded-cpp expect=clean */
namespace std {
template <typename T>
struct array {
  T slot;
};
} // namespace std

int first(int n) {
  std::array<int> values = {};
  values = std::array<int>{};
  return n + values.slot;
}

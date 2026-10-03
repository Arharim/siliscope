/* ss-run: profile=embedded-cpp expect=ss.cpp.no-vector-bool */
namespace std {
template <typename T>
struct vector {
  unsigned size() const;
};
} // namespace std

using Bits = std::vector<bool>;

struct Holder {
  int count() const { return bits_.size() ? 1 : 0; }

private:
  std::vector<bool> bits_;
};

std::vector<bool> make() { return std::vector<bool>(); }

int use(std::vector<bool> &bits) {
  Bits named;
  named = Bits();
  bits = named;
  return static_cast<int>(bits.size() + named.size());
}

/* ss-run: profile=embedded-cpp expect=clean */
namespace mine {
template <typename T>
struct vector {
  T get() const { return slot_; }

private:
  T slot_{};
};
} // namespace mine

int fill() {
  unsigned char bits[4] = {0, 0, 0, 0};
  bits[0] = 1;
  mine::vector<bool> local;
  local = mine::vector<bool>{};
  return static_cast<int>(bits[0]) + (local.get() ? 1 : 0);
}

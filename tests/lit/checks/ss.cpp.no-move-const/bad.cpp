/* ss-run: profile=embedded-cpp expect=ss.cpp.no-move-const */
namespace std {
template <typename T>
T &&move(T &&value) noexcept {
  return static_cast<T &&>(value);
}
} // namespace std

struct Widget {
  int value;
};

Widget pass(const Widget item) { return std::move(item); }

Widget passRef(const Widget &item) { return std::move(item); }

Widget passLocal() {
  const Widget item{0};
  return std::move(item);
}

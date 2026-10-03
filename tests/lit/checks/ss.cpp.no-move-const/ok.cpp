/* ss-run: profile=embedded-cpp expect=clean */
namespace std {
template <typename T>
T &&move(T &&value) noexcept {
  value = static_cast<T &&>(value);
  return static_cast<T &&>(value);
}

template <typename T>
const T &forward(const T &value) noexcept {
  return value;
}
} // namespace std

template <typename T>
const T &move(const T &value) noexcept {
  return value;
}

struct Widget {
  int value;
};

Widget pass(Widget item) { return std::move(item); }

Widget passForward(const Widget item) { return std::forward(item); }

Widget passLocal(const Widget item) { return move(item); }

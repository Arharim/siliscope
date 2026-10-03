/* ss-run: profile=embedded-cpp expect=clean */
namespace tool {
constexpr int value = 1;
}
using tool::value;

int read() { return value; }

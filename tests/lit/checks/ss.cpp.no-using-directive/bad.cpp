/* ss-run: profile=embedded-cpp expect=ss.cpp.no-using-directive */
namespace tool {
constexpr int value = 1;
}
using namespace tool;

int read() { return value; }

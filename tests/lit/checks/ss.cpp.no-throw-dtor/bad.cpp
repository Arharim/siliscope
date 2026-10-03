/* ss-run: profile=embedded-cpp expect=ss.cpp.no-throw-dtor */
struct Boom {
  ~Boom() noexcept(false) {}
};

struct Throws {
  ~Throws() { throw 1; }
};

struct Holds {
  Boom boom;
};

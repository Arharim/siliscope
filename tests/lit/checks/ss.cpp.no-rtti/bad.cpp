/* ss-run: profile=embedded-cpp expect=ss.cpp.no-rtti */
struct Base {
  virtual int id() const { return 1; }
};

struct Derived : Base {
  int id() const override { return 2; }
};

int kind(Base *b) {
  if (dynamic_cast<Derived *>(b) != nullptr) {
    return 1;
  }
  return typeid(*b) == typeid(Derived) ? 2 : 0;
}

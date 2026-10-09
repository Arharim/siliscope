/* ss-run: profile=strict expect=clean */
struct Box {
  void use(int);
};

void Box::use(int count) { count = 0; }

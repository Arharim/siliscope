/* ss-run: profile=strict expect=ss.fn.param-names-consistent */
struct Box {
  void use(int count);
};

void Box::use(int n) { n = 0; }

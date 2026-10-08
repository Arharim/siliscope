/* ss-companion */
struct Machine {
  void step();
  void again();
};

void Machine::again() { step(); }

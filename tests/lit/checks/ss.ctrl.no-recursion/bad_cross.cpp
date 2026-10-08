/* ss-run: also=other.cpp expect=ss.ctrl.no-recursion */
struct Machine {
  void step();
  void again();
};

void Machine::step() { again(); }

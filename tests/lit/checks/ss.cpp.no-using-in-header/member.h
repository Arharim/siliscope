#pragma once

struct Base {
  int id() const { return 1; }
};

struct Derived : Base {
  using Base::id;
};

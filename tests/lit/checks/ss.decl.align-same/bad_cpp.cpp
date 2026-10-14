/* ss-run: profile=embedded-cpp expect=ss.decl.align-same */
alignas(8) extern int slot;
int slot = 1;

int main() {
  slot = 2;
  return slot;
}

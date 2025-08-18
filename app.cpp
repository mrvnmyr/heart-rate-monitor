#include <iostream>
// Avoid importing the module to sidestep BMI build-order issues with GCC.
// We just link to the exported symbol instead.
extern int run();

int main() {
  std::cerr << "[dbg] app.cpp: entering main(), calling run()...\n";
  int rc = run();
  std::cerr << "[dbg] app.cpp: run() returned " << rc << "\n";
  return rc;
}

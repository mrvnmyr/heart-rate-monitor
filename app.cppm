module; // global module fragment for legacy includes
#include <iostream>

// Named module (interface form is fine; we don't export anything).
export module app;

// We deliberately avoid importing the polarh9 module to keep BMI handling
// out of the build; we just link to the exported symbol instead.
extern int run();

int main() {
  std::cerr << "[dbg] app.cppm: entering main(), calling run()...\n";
  int rc = run();
  std::cerr << "[dbg] app.cppm: run() returned " << rc << "\n";
  return rc;
}

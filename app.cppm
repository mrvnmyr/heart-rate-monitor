module; // global module fragment for legacy includes and non-module decls
#include <iostream>
// Declare run() in the global module fragment so it keeps normal linkage
// (otherwise Clang mangles it with module ownership, e.g. run@app()).
extern "C++" int run();

// Named module (interface form is fine; we don't export anything).
export module app;

int main() {
  std::cerr << "[dbg] app.cppm: entering main(), compiler=" << __VERSION__
            << ", file=" << __FILE__ << "\n";
  int rc = run();
  std::cerr << "[dbg] app.cppm: run() returned " << rc << "\n";
  return rc;
}

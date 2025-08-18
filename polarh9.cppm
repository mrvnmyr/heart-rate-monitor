// Declaration-only interface to keep a named module in the build.
export module polarh9;

// The implementation (including the definition of run()) lives in
// polarh9_impl.cpp to avoid GCC BMI/link-order pitfalls.
export int run();

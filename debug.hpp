#pragma once
#include <iostream>
extern bool g_debug;
#define DBG if (::g_debug) std::cerr

#pragma once
#include <ctime>
#include <iostream>

extern bool g_debug;

inline const char* timestamp_now_s() {
  static thread_local char buf[20];
  std::time_t t = std::time(nullptr);
  std::tm tm {};
  localtime_r(&t, &tm);
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

#define ERR (std::cerr << "[" << timestamp_now_s() << "] ")
#define DBG if (::g_debug) ERR

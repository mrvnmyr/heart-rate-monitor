#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "debug.hpp"

extern bool g_health_warnings;
extern std::string g_health_warning_prefix;
extern long long g_health_warning_ts_ms;

inline void health_emit_warning(std::string_view msg) {
  const char* ts = (g_health_warning_ts_ms >= 0)
    ? timestamp_from_ms(g_health_warning_ts_ms)
    : timestamp_now_s();
  std::cerr << "[" << ts << "] " << '\a' << "[warn] ";
  if (!g_health_warning_prefix.empty()) {
    std::cerr << "[" << g_health_warning_prefix << "] ";
  }
  std::cerr << msg << "\n";
}

void health_check_bradycardia(int bpm, long long ts_ms);
void health_check_tachycardia(int bpm, long long ts_ms);
void health_check_arrhythmia(const std::vector<int>& rr_ms);

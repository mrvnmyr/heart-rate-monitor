#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "debug.hpp"

extern bool g_health_warnings;
extern std::string g_health_warning_prefix;

inline void health_emit_warning(std::string_view msg) {
  ERR << '\a' << "[warn] ";
  if (!g_health_warning_prefix.empty()) {
    ERR << "[" << g_health_warning_prefix << "] ";
  }
  ERR << msg << "\n";
}

void health_check_bradycardia(int bpm);
void health_check_tachycardia(int bpm);
void health_check_arrhythmia(const std::vector<int>& rr_ms);

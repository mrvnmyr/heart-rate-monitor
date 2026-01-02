#pragma once
#include <string_view>
#include <vector>

#include "debug.hpp"

extern bool g_health_warnings;

inline void health_emit_warning(std::string_view msg) {
  ERR << '\a' << "[warn] " << msg << "\n";
}

void health_check_bradycardia(int bpm);
void health_check_tachycardia(int bpm);
void health_check_arrhythmia(const std::vector<int>& rr_ms);

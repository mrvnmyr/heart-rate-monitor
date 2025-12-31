#include "health.hpp"

#include <string>

void health_check_bradycardia(int bpm) {
  static bool s_active = false;
  bool now = (bpm > 0) && (bpm < 60);
  if (now && !s_active) {
    health_emit_warning("Bradycardia: bpm < 60 (" + std::to_string(bpm) + ")");
  }
  s_active = now;
}

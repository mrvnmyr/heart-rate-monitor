#include "health.hpp"

#include <string>

void health_check_tachycardia(int bpm) {
  static bool s_active = false;
  bool now = (bpm > 0) && (bpm > 100);
  if (now && !s_active) {
    health_emit_warning("Tachycardia: bpm > 100 (" + std::to_string(bpm) + ")");
  }
  s_active = now;
}

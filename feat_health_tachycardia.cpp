#include "feat_health.hpp"

#include <algorithm>
#include <sstream>
#include <string>

void health_check_tachycardia(int bpm, long long ts_ms) {
  static bool s_active = false;
  static long long s_start_ms = 0;
  static int s_highest_bpm = 0;
  bool now = (bpm > 0) && (bpm > 100);
  if (now && !s_active) {
    s_start_ms = ts_ms;
    s_highest_bpm = bpm;
    health_emit_warning("Tachycardia (bpm > 100)");
  } else if (now && s_active) {
    s_highest_bpm = std::max(s_highest_bpm, bpm);
  } else if (!now && s_active) {
    long long dur_ms = (ts_ms >= s_start_ms) ? (ts_ms - s_start_ms) : 0;
    if (dur_ms > 1000) {
      std::ostringstream oss;
      oss << "Tachycardia recovered: highest_bpm=" << s_highest_bpm
          << " duration=" << health_format_duration(dur_ms);
      health_emit_warning(oss.str());
    }
  }
  s_active = now;
}

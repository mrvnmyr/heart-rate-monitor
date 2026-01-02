#include "feat_health.hpp"

#include <algorithm>
#include <iomanip>
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
    health_emit_warning("Tachycardia: bpm > 100 (" + std::to_string(bpm) + ")");
  } else if (now && s_active) {
    s_highest_bpm = std::max(s_highest_bpm, bpm);
  } else if (!now && s_active) {
    long long dur_ms = (ts_ms >= s_start_ms) ? (ts_ms - s_start_ms) : 0;
    double dur_s = static_cast<double>(dur_ms) / 1000.0;
    std::ostringstream oss;
    oss << "Recovered from tachycardia duration="
        << std::fixed << std::setprecision(1) << dur_s
        << "s highest_bpm=" << s_highest_bpm;
    health_emit_warning(oss.str());
  }
  s_active = now;
}

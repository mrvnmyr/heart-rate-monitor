#include "feat_health.hpp"

#include <sstream>

std::string health_format_duration(long long ms) {
  long long total_s = (ms >= 0) ? (ms / 1000) : 0;
  long long mins = total_s / 60;
  long long secs = total_s % 60;
  std::ostringstream oss;
  if (mins > 0) {
    oss << mins << "m" << secs << "s";
  } else {
    oss << secs << "s";
  }
  return oss.str();
}

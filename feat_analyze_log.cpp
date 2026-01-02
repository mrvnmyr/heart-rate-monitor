#include <cctype>
#include <fstream>
#include <string>
#include <vector>

#include "feat_health.hpp"
#include "feat_analyze_log.hpp"

extern std::string g_health_warning_prefix;
extern long long g_health_warning_ts_ms;

namespace {

bool parse_log_line(const std::string& line, std::vector<long long>* out) {
  out->clear();
  if (line.empty()) return false;
  size_t i = 0;
  while (i < line.size()) {
    if (!std::isdigit(static_cast<unsigned char>(line[i]))) return false;
    long long v = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
      v = v * 10 + (line[i] - '0');
      ++i;
    }
    out->push_back(v);
    if (i == line.size()) break;
    if (line[i] != ',') return false;
    ++i;
  }
  return out->size() >= 2;
}

}  // namespace

int analyze_log(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    ERR << "[err] Unable to open log file: " << path << "\n";
    return EXIT_FAILURE;
  }

  std::string line;
  std::vector<long long> fields;
  while (std::getline(in, line)) {
    if (!parse_log_line(line, &fields)) continue;
    long long ts = fields[0];
    int bpm = static_cast<int>(fields[1]);
    std::vector<int> rr_ms;
    rr_ms.reserve(fields.size() > 2 ? (fields.size() - 2) : 0);
    for (size_t i = 2; i < fields.size(); ++i) {
      rr_ms.push_back(static_cast<int>(fields[i]));
    }

    g_health_warning_prefix = "ts=" + std::to_string(ts);
    g_health_warning_ts_ms = ts;
    health_check_bradycardia(bpm, ts);
    health_check_tachycardia(bpm, ts);
    if (!rr_ms.empty()) {
      health_check_arrhythmia(rr_ms, ts);
    }
    g_health_warning_prefix.clear();
    g_health_warning_ts_ms = -1;
  }

  return 0;
}

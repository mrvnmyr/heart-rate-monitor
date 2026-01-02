#include "feat_health.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

namespace {

constexpr int kMinRRms = 250;
constexpr int kMaxRRms = 2500;
constexpr size_t kAfWindow = 128;
constexpr size_t kMaxRawRR = 512;

double rmssd_ratio(const std::vector<double>& rr) {
  if (rr.size() < 2) return std::numeric_limits<double>::quiet_NaN();
  double sum = 0.0;
  for (size_t i = 0; i + 1 < rr.size(); ++i) {
    double d = rr[i + 1] - rr[i];
    sum += d * d;
  }
  double rmssd = std::sqrt(sum / static_cast<double>(rr.size() - 1));
  double meanrr = std::accumulate(rr.begin(), rr.end(), 0.0) / rr.size();
  return (meanrr > 0.0) ? (rmssd / meanrr) : std::numeric_limits<double>::quiet_NaN();
}

double turning_point_ratio(const std::vector<double>& rr) {
  if (rr.size() < 3) return std::numeric_limits<double>::quiet_NaN();
  size_t tp = 0;
  for (size_t i = 1; i + 1 < rr.size(); ++i) {
    if ((rr[i] > rr[i - 1] && rr[i] > rr[i + 1]) ||
        (rr[i] < rr[i - 1] && rr[i] < rr[i + 1])) {
      ++tp;
    }
  }
  return static_cast<double>(tp) / static_cast<double>(rr.size() - 2);
}

double shannon_entropy_16bins(const std::vector<double>& rr) {
  if (rr.size() < 32) return std::numeric_limits<double>::quiet_NaN();
  std::vector<double> s = rr;
  std::sort(s.begin(), s.end());
  std::vector<double> trimmed(s.begin() + 8, s.end() - 8);
  double lo = trimmed.front();
  double hi = trimmed.back();
  if (hi <= lo) return 0.0;

  constexpr int bins = 16;
  int counts[bins] = {0};
  for (double x : trimmed) {
    double t = (x - lo) / (hi - lo);
    int k = static_cast<int>(t * bins);
    if (k < 0) k = 0;
    if (k >= bins) k = bins - 1;
    counts[k] += 1;
  }

  int total = static_cast<int>(trimmed.size());
  if (total == 0) return std::numeric_limits<double>::quiet_NaN();

  double se = 0.0;
  for (int c : counts) {
    if (c == 0) continue;
    double p = static_cast<double>(c) / static_cast<double>(total);
    se -= p * std::log(p);
  }
  se /= std::log(1.0 / bins);
  return se;
}

std::vector<double> dash_style_clean_rr(const std::vector<int>& rr) {
  if (rr.size() < 5) {
    std::vector<double> out;
    out.reserve(rr.size());
    for (int v : rr) out.push_back(static_cast<double>(v));
    return out;
  }

  std::vector<bool> keep(rr.size(), true);
  for (size_t i = 1; i + 2 < rr.size(); ++i) {
    double r_prev = static_cast<double>(rr[i]) / rr[i - 1];
    double r_next = static_cast<double>(rr[i + 1]) / rr[i];
    double r_next2 = static_cast<double>(rr[i + 2]) / rr[i + 1];
    if ((r_prev <= 0.8) && (r_next >= 1.3) && (r_next2 <= 0.9)) {
      keep[i] = false;
      keep[i + 1] = false;
      i += 1;
    }
  }

  std::vector<double> out;
  out.reserve(rr.size());
  for (size_t i = 0; i < rr.size(); ++i) {
    if (keep[i]) out.push_back(static_cast<double>(rr[i]));
  }
  return out;
}

void warn_pause_or_artifact(int rr_ms) {
  double hr_bpm = (rr_ms > 0) ? (60000.0 / static_cast<double>(rr_ms)) : 0.0;
  std::ostringstream oss;
  if (rr_ms > kMaxRRms) {
    oss << "Arrhythmia: pause/dropout candidate rr_ms=" << rr_ms
        << " hr_bpm=" << std::fixed << std::setprecision(1) << hr_bpm;
  } else {
    oss << "Arrhythmia: artifact candidate rr_ms=" << rr_ms
        << " hr_bpm=" << std::fixed << std::setprecision(1) << hr_bpm;
  }
  health_emit_warning(oss.str());
}

void warn_ectopic_pattern(int a, int b, int c, int d) {
  std::ostringstream oss;
  oss << "Arrhythmia: ectopic-like short-long pattern rr_ms=["
      << a << "," << b << "," << c << "," << d << "]";
  health_emit_warning(oss.str());
}

std::string fmt_metric(double v) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3) << v;
  return oss.str();
}

}  // namespace

void health_check_arrhythmia(const std::vector<int>& rr_ms, long long ts_ms) {
  static std::deque<int> s_rr_raw;
  static bool s_possible_af = false;
  static long long s_af_start_ms = 0;
  static bool s_pause_active = false;
  static long long s_pause_start_ms = 0;
  static int s_pause_min_rr = 0;
  static int s_pause_max_rr = 0;
  static bool s_ectopic_active = false;
  static long long s_ectopic_start_ms = 0;
  static int s_ectopic_count = 0;

  for (int rr : rr_ms) {
    if (rr < kMinRRms || rr > kMaxRRms) {
      if (!s_pause_active) {
        s_pause_active = true;
        s_pause_start_ms = ts_ms;
        s_pause_min_rr = rr;
        s_pause_max_rr = rr;
      } else {
        s_pause_min_rr = std::min(s_pause_min_rr, rr);
        s_pause_max_rr = std::max(s_pause_max_rr, rr);
      }
      warn_pause_or_artifact(rr);
      continue;
    } else if (s_pause_active) {
      long long dur_ms = (ts_ms >= s_pause_start_ms) ? (ts_ms - s_pause_start_ms) : 0;
      if (dur_ms > 1000) {
        std::ostringstream oss;
        oss << "Arrhythmia recovered: pause/artifact"
            << " duration=" << health_format_duration(dur_ms)
            << " min_rr=" << s_pause_min_rr
            << " max_rr=" << s_pause_max_rr;
        health_emit_warning(oss.str());
      }
      s_pause_active = false;
    }
    s_rr_raw.push_back(rr);
    if (s_rr_raw.size() > kMaxRawRR) s_rr_raw.pop_front();

    if (s_rr_raw.size() >= 4) {
      size_t n = s_rr_raw.size();
      int a = s_rr_raw[n - 4];
      int b = s_rr_raw[n - 3];
      int c = s_rr_raw[n - 2];
      int d = s_rr_raw[n - 1];
      double r_prev = static_cast<double>(b) / a;
      double r_next = static_cast<double>(c) / b;
      double r_next2 = static_cast<double>(d) / c;
      if ((r_prev <= 0.8) && (r_next >= 1.3) && (r_next2 <= 0.9)) {
        if (!s_ectopic_active) {
          s_ectopic_active = true;
          s_ectopic_start_ms = ts_ms;
          s_ectopic_count = 0;
        }
        ++s_ectopic_count;
        warn_ectopic_pattern(a, b, c, d);
      } else if (s_ectopic_active) {
        long long dur_ms = (ts_ms >= s_ectopic_start_ms) ? (ts_ms - s_ectopic_start_ms) : 0;
        if (dur_ms > 1000) {
          std::ostringstream oss;
          oss << "Arrhythmia recovered: ectopic"
              << " duration=" << health_format_duration(dur_ms)
              << " count=" << s_ectopic_count;
          health_emit_warning(oss.str());
        }
        s_ectopic_active = false;
      }
    }
  }

  if (s_rr_raw.size() < kAfWindow) {
    if (s_possible_af) {
      long long dur_ms = (ts_ms >= s_af_start_ms) ? (ts_ms - s_af_start_ms) : 0;
      if (dur_ms > 1000) {
        std::ostringstream oss;
        oss << "Arrhythmia recovered: possible AF"
            << " duration=" << health_format_duration(dur_ms);
        health_emit_warning(oss.str());
      }
    }
    s_possible_af = false;
    return;
  }

  std::vector<int> raw(s_rr_raw.begin(), s_rr_raw.end());
  std::vector<double> cleaned = dash_style_clean_rr(raw);
  if (cleaned.size() < kAfWindow) {
    if (s_possible_af) {
      long long dur_ms = (ts_ms >= s_af_start_ms) ? (ts_ms - s_af_start_ms) : 0;
      if (dur_ms > 1000) {
        std::ostringstream oss;
        oss << "Arrhythmia recovered: possible AF"
            << " duration=" << health_format_duration(dur_ms);
        health_emit_warning(oss.str());
      }
    }
    s_possible_af = false;
    return;
  }

  std::vector<double> seg(cleaned.end() - kAfWindow, cleaned.end());
  double r = rmssd_ratio(seg);
  double t = turning_point_ratio(seg);
  double e = shannon_entropy_16bins(seg);
  bool possible = (r > 0.1) && (t > 0.54) && (t < 0.77) && (e > 0.7);

  if (possible && !s_possible_af) {
    s_af_start_ms = ts_ms;
    std::ostringstream oss;
    oss << "Arrhythmia: possible AF (RR-only screening)"
        << " rmssd_ratio=" << fmt_metric(r)
        << " tpr=" << fmt_metric(t)
        << " se=" << fmt_metric(e);
    health_emit_warning(oss.str());
  } else if (!possible && s_possible_af) {
    long long dur_ms = (ts_ms >= s_af_start_ms) ? (ts_ms - s_af_start_ms) : 0;
    if (dur_ms > 1000) {
      std::ostringstream oss;
      oss << "Arrhythmia recovered: possible AF"
          << " duration=" << health_format_duration(dur_ms);
      health_emit_warning(oss.str());
    }
  }
  s_possible_af = possible;
}

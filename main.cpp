#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <chrono>

#include "debug.hpp"
#include "bluetooth.hpp"
#include "device_polar.hpp"

using namespace std::chrono_literals;

bool g_debug = false;  // defined for debug.hpp / other TUs
bool g_health_warnings = false;

static void print_help(const char* prog) {
  const char* p = (prog && *prog) ? prog : "polarm";
  std::cout
    << "polarm — Minimal Polar H9/H10 heart-rate recorder\n\n"
    << "Usage: " << p << " [options]\n\n"
    << "Options:\n"
    << "  -h, --help      Show this help and exit\n"
    << "  -d, --debug     Verbose debug logs to stderr\n"
    << "  --health-warnings  Emit brady/tachy/arrhythmia warnings\n\n"
    << "Output:\n"
    << "  Lines to stdout in the form: <epoch_ms>,<bpm>[,<rr_ms>...]\n"
    << "  RR values are converted from 1/1024 s ticks to milliseconds.\n";
}

static int run_impl() {
  DBG << "[dbg] run_impl(): starting\n";
  DBG << "[dbg] assuming default adapter at /org/bluez/hci0\n";
  Bus bus;

  // Prefer H10 if both appear
  std::vector<std::string_view> names = { polar_h10_name(), polar_h9_name() };
  DBG << "[dbg] target device names (priority order): '"
      << names[0] << "', '" << names[1] << "'\n";
  auto dev = find_any_device_by_names(bus, names);

  // Scan if not found
  if (!dev) {
    ERR << "[info] Starting discovery...\n";
    if (start_adapter_discovery(bus) < 0) {
      ERR << "[err] StartDiscovery failed\n";
      return EXIT_FAILURE;
    }
    auto deadline = std::chrono::steady_clock::now() + 90s;
    int iter = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(2s);
      dev = find_any_device_by_names(bus, names);
      if (dev) break;
      DBG << "[dbg] scan iteration " << (++iter) << " ... not yet found\n";
    }
    stop_adapter_discovery(bus);
    if (!dev) {
      ERR << "[err] Device not found after scan.\n";
      return EXIT_FAILURE;
    }
  }

  ERR << "[info] Found device: " << dev->name << " path: " << dev->path << "\n";

  // Connect if needed
  if (!get_device_connected(bus, dev->path)) {
    ERR << "[info] Connecting...\n";
    if (call_void(bus, dev->path, "org.bluez.Device1", "Connect") < 0) {
      ERR << "[err] Connect failed\n";
      return EXIT_FAILURE;
    }
    auto deadline = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (get_device_connected(bus, dev->path)) break;
      std::this_thread::sleep_for(500ms);
    }
    if (!get_device_connected(bus, dev->path)) {
      ERR << "[err] Failed to connect (timeout).\n";
      return EXIT_FAILURE;
    }
  }
  ERR << "[info] Connected.\n";

  // Find Heart Rate Measurement characteristic (retryable on some devices)
  static constexpr std::string_view kHRCharUUID =
    "00002a37-0000-1000-8000-00805f9b34fb";
  DBG << "[dbg] searching for HRM characteristic uuid=" << kHRCharUUID << "\n";
  auto ch_path_opt = find_char_by_uuid(bus, dev->path, kHRCharUUID);
  if (!ch_path_opt) {
    ERR << "[warn] Heart Rate Measurement characteristic not found; retrying...\n";
    auto deadline = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (!get_device_connected(bus, dev->path)) {
        ERR << "[info] Reconnecting while waiting for HR characteristic...\n";
        if (call_void(bus, dev->path, "org.bluez.Device1", "Connect") < 0) {
          ERR << "[warn] Connect failed during HR characteristic retry.\n";
        } else {
          auto conn_deadline = std::chrono::steady_clock::now() + 20s;
          while (std::chrono::steady_clock::now() < conn_deadline) {
            if (get_device_connected(bus, dev->path)) break;
            std::this_thread::sleep_for(500ms);
          }
          if (get_device_connected(bus, dev->path)) {
            ERR << "[info] Connected (retry).\n";
          } else {
            ERR << "[warn] Connect timeout during HR characteristic retry.\n";
          }
        }
      }

      ch_path_opt = find_char_by_uuid(bus, dev->path, kHRCharUUID);
      if (ch_path_opt) break;
      std::this_thread::sleep_for(1s);
    }
    if (!ch_path_opt) {
      ERR << "[err] Heart Rate Measurement characteristic not found (timeout).\n";
      return EXIT_FAILURE;
    }
  }
  std::string ch_path = *ch_path_opt;
  ERR << "[info] Heart Rate characteristic: " << ch_path << "\n";

  // Start notifications
  if (start_notify(bus, ch_path) < 0) {
    ERR << "[err] StartNotify failed\n";
    return EXIT_FAILURE;
  }
  DBG << "[dbg] StartNotify returned ok; subscribing to PropertiesChanged\n";

  // Subscribe to PropertiesChanged on that HR path
  std::string match =
    "type='signal',"
    "sender='org.bluez',"
    "interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',"
    "path='" + ch_path + "'";
  DBG << "[dbg] Installing HR D-Bus match: " << match << "\n";

  sd_bus_slot* slot = nullptr;
  int r = sd_bus_add_match(bus, &slot, match.c_str(), props_changed_cb, nullptr);
  if (r < 0) {
    ERR << "[err] sd_bus_add_match failed: " << -r << "\n";
    return EXIT_FAILURE;
  }

  ERR << "[info] Listening for BPM/RR notifications (Ctrl+C to quit)...\n";
  // Event loop with maintenance (0.5s tick)
  for (;;) {
    r = sd_bus_process(bus, nullptr);
    if (r < 0) {
      ERR << "[fatal] sd_bus_process: " << -r << "\n";
      return EXIT_FAILURE;
    }
    if (r == 0) {
      ensure_connected_and_notifying(bus, dev->path, ch_path, slot, names);
      r = sd_bus_wait(bus, 500000); // 0.5s
      if (r < 0) {
        ERR << "[fatal] sd_bus_wait: " << -r << "\n";
        return EXIT_FAILURE;
      }
    }
  }
}

int main(int argc, char** argv) {
  bool show_help = false;

  // Parse flags
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-d" || std::string_view(argv[i]) == "--debug") {
      g_debug = true;
    } else if (std::string_view(argv[i]) == "--health-warnings") {
      g_health_warnings = true;
    } else if (std::string_view(argv[i]) == "-h" || std::string_view(argv[i]) == "--help") {
      show_help = true;
    }
  }

  if (show_help) {
    print_help(argv[0]);
    return 0;
  }

  DBG << "[dbg] main(): debug enabled\n";
  DBG << "[dbg] main(): compiler=" << __VERSION__
      << ", __cplusplus=" << __cplusplus << ", file=" << __FILE__ << "\n";
  int rc = run_impl();
  DBG << "[dbg] main(): run_impl() returned " << rc << "\n";
  return rc;
}

#include <iostream>
#include <string_view>
#include <vector>
#include <thread>
#include <chrono>

#include "debug.hpp"
#include "bluetooth.hpp"
#include "device_polar.hpp"

using namespace std::chrono_literals;

bool g_debug = false;  // defined for debug.hpp / other TUs

static int run_impl() {
  DBG << "[dbg] run_impl(): starting\n";
  Bus bus;

  // Prefer H10 if both appear
  std::vector<std::string_view> names = { polar_h10_name(), polar_h9_name() };
  auto dev = find_any_device_by_names(bus, names);

  // Scan if not found
  if (!dev) {
    std::cerr << "[info] Starting discovery...\n";
    if (start_adapter_discovery(bus) < 0) {
      std::cerr << "[err] StartDiscovery failed\n";
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
      std::cerr << "[err] Device not found after scan.\n";
      return EXIT_FAILURE;
    }
  }

  std::cerr << "[info] Found device: " << dev->name << " path: " << dev->path << "\n";

  // Connect if needed
  if (!get_device_connected(bus, dev->path)) {
    std::cerr << "[info] Connecting...\n";
    if (call_void(bus, dev->path, "org.bluez.Device1", "Connect") < 0) {
      std::cerr << "[err] Connect failed\n";
      return EXIT_FAILURE;
    }
    auto deadline = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (get_device_connected(bus, dev->path)) break;
      std::this_thread::sleep_for(500ms);
    }
    if (!get_device_connected(bus, dev->path)) {
      std::cerr << "[err] Failed to connect (timeout).\n";
      return EXIT_FAILURE;
    }
  }
  std::cerr << "[info] Connected.\n";

  // Find Heart Rate Measurement characteristic
  static constexpr std::string_view kHRCharUUID =
    "00002a37-0000-1000-8000-00805f9b34fb";
  auto ch_path_opt = find_char_by_uuid(bus, dev->path, kHRCharUUID);
  if (!ch_path_opt) {
    std::cerr << "[err] Heart Rate Measurement characteristic not found.\n";
    return EXIT_FAILURE;
  }
  std::string ch_path = *ch_path_opt;
  std::cerr << "[info] Heart Rate characteristic: " << ch_path << "\n";

  // Start notifications
  if (start_notify(bus, ch_path) < 0) {
    std::cerr << "[err] StartNotify failed\n";
    return EXIT_FAILURE;
  }

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
    std::cerr << "[err] sd_bus_add_match failed: " << -r << "\n";
    return EXIT_FAILURE;
  }

  std::cerr << "[info] Listening for BPM/RR notifications (Ctrl+C to quit)...\n";
  // Event loop with maintenance (0.5s tick)
  for (;;) {
    r = sd_bus_process(bus, nullptr);
    if (r < 0) {
      std::cerr << "[fatal] sd_bus_process: " << -r << "\n";
      return EXIT_FAILURE;
    }
    if (r == 0) {
      ensure_connected_and_notifying(bus, dev->path, ch_path, slot, names);
      r = sd_bus_wait(bus, 500000); // 0.5s
      if (r < 0) {
        std::cerr << "[fatal] sd_bus_wait: " << -r << "\n";
        return EXIT_FAILURE;
      }
    }
  }
}

int main(int argc, char** argv) {
  // Parse flags: -d or --debug enables [dbg] messages (stderr)
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-d" || std::string_view(argv[i]) == "--debug") {
      g_debug = true;
    }
  }
  DBG << "[dbg] main(): debug enabled\n";
  DBG << "[dbg] main(): compiler=" << __VERSION__
      << ", __cplusplus=" << __cplusplus << ", file=" << __FILE__ << "\n";
  int rc = run_impl();
  DBG << "[dbg] main(): run_impl() returned " << rc << "\n";
  return rc;
}

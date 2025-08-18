#ifdef __ANDROID__
#include <iostream>
int main() {
  std::cerr << "[info] Android build: BlueZ D-Bus not available. "
               "This binary is a build-time stub.\n";
  return 0;
}
#else
// Single-translation-unit implementation (no headers, no forward decls).
#include <systemd/sd-bus.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <memory>

namespace {

using namespace std::chrono_literals;

// static constexpr auto poll_time = 200000; // 0.2s
static constexpr auto poll_time = 500000; // 0.5s
// static constexpr auto poll_time = 1000000; // 1.0s

// ---- debug flag and helper ----
static bool g_debug = false;
#define DBG if (::g_debug) std::cerr

// ---- constants ----
static constexpr std::string_view kBluezService = "org.bluez";
static constexpr std::string_view kObjManager = "org.freedesktop.DBus.ObjectManager";
static constexpr std::string_view kProps = "org.freedesktop.DBus.Properties";
static constexpr std::string_view kGattChar1 = "org.bluez.GattCharacteristic1";
static constexpr std::string_view kDevice1 = "org.bluez.Device1";
static constexpr std::string_view kAdapter1 = "org.bluez.Adapter1";

static constexpr std::string_view kAdapterPath = "/org/bluez/hci0";
static constexpr std::string_view kTargetNameH9  = "Polar H9 EA190E24";
static constexpr std::string_view kTargetNameH10 = "Polar H10 8A8F192B";
static constexpr std::string_view kHRCharUUID = "00002a37-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kBattUUID  = "00002a19-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kBodyLocUUID = "00002a38-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kManufNameUUID = "00002a29-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kModelNumUUID  = "00002a24-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kHwRevUUID     = "00002a27-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kFwRevUUID     = "00002a26-0000-1000-8000-00805f9b34fb";
static constexpr std::string_view kSwRevUUID     = "00002a28-0000-1000-8000-00805f9b34fb";

[[noreturn]] void die(const char* msg, int err) {
  std::cerr << "[fatal] " << msg << " (" << -err << "): " << strerror(-err) << "\n";
  std::exit(EXIT_FAILURE);
}

std::string getenv_or(const char* key, const char* defv) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string(defv);
}

std::string prefix_for_name(std::string_view name) {
  if (name.find("H10") != std::string_view::npos) return "polarh10";
  return "polarh9";
}

std::string out_file_path(const std::string& prefix) {
  std::string home = getenv_or("HOME", ".");
  std::filesystem::path p = std::filesystem::path(home) / ".cache" / prefix;
  return p.string();
}
std::string out_rr_file_path(const std::string& prefix) {
  std::string home = getenv_or("HOME", ".");
  std::filesystem::path p = std::filesystem::path(home) / ".cache" / (prefix + "rr");
  return p.string();
}
std::string out_suffix_file_path(const std::string& prefix, const std::string& suffix) {
  std::string home = getenv_or("HOME", ".");
  std::filesystem::path p = std::filesystem::path(home) / ".cache" / (prefix + "_" + suffix);
  return p.string();
}

void ensure_parent_dir(const std::string& file) {
  std::filesystem::path p(file);
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  if (ec) {
    std::cerr << "[warn] create_directories failed: " << ec.message() << "\n";
  }
}

std::string to_hex(const std::vector<uint8_t>& v) {
  std::ostringstream oss;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) oss << ' ';
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)v[i];
  }
  return oss.str();
}

uint64_t now_ms() {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch());
  return (uint64_t)ms.count();
}

std::string to_lower_uuid(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c){return (char)std::tolower(c);});
  return s;
}

struct Bus {
  sd_bus* bus{};
  Bus() {
    int r = sd_bus_open_system(&bus);
    if (r < 0) die("sd_bus_open_system", r);
    DBG << "[dbg] sd_bus_open_system() ok\n";
  }
  ~Bus() { if (bus) sd_bus_unref(bus); }
  operator sd_bus*() const { return bus; }
};

// Minimal object entry we care about
struct ManagedObjectsEntry {
  std::string path;
  std::string interface;
  std::optional<std::string> name;
  std::optional<std::string> uuid;
};

std::vector<ManagedObjectsEntry> get_managed_objects(sd_bus* bus) {
  sd_bus_message* m = nullptr;
  sd_bus_message* reply = nullptr;

  int r = sd_bus_message_new_method_call(
    bus, &m, std::string(kBluezService).c_str(), "/",
    std::string(kObjManager).c_str(), "GetManagedObjects");
  if (r < 0) die("sd_bus_message_new_method_call(GetManagedObjects)", r);

  r = sd_bus_call(bus, m, 0, nullptr, &reply);
  sd_bus_message_unref(m);
  if (r < 0) die("sd_bus_call(GetManagedObjects)", r);

  std::vector<ManagedObjectsEntry> out;

  // Signature: a{oa{sa{sv}}}
  r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
  if (r < 0) die("enter a{oa{sa{sv}}}", r);

  while ((r = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0) {
    const char* obj_path = nullptr;
    r = sd_bus_message_read_basic(reply, 'o', &obj_path);
    if (r < 0) die("read object path", r);
    DBG << "[dbg] MO obj: " << (obj_path ? obj_path : "(null)") << "\n";

    r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
    if (r < 0) die("enter a of interfaces", r);

    while ((r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
      const char* iface = nullptr;
      r = sd_bus_message_read_basic(reply, 's', &iface);
      if (r < 0) die("read interface name", r);

      r = sd_bus_message_enter_container(reply, 'a', "{sv}");
      if (r < 0) die("enter a{sv}", r);

      ManagedObjectsEntry entry;
      entry.path = obj_path ? obj_path : "";
      entry.interface = iface ? iface : "";

      while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char* prop = nullptr;
        r = sd_bus_message_read_basic(reply, 's', &prop);
        if (r < 0) die("read property name", r);

        char vt;
        const char* vtsig = nullptr;
        r = sd_bus_message_peek_type(reply, &vt, &vtsig);
        if (r < 0) die("peek variant type", r);

        if (vtsig && std::strcmp(vtsig, "s") == 0) {
          r = sd_bus_message_enter_container(reply, 'v', "s");
          if (r < 0) die("enter v(s)", r);
          const char* sval = nullptr;
          r = sd_bus_message_read_basic(reply, 's', &sval);
          if (r < 0) die("read v.s", r);
          r = sd_bus_message_exit_container(reply);
          if (r < 0) die("exit v(s)", r);

          if (prop && std::strcmp(prop, "Name") == 0) {
            entry.name = sval ? std::string(sval) : std::string();
            DBG << "[dbg]       Name=" << *entry.name << "\n";
          } else if (prop && std::strcmp(prop, "UUID") == 0) {
            std::string u = sval ? std::string(sval) : std::string();
            entry.uuid = to_lower_uuid(std::move(u));
            DBG << "[dbg]       UUID=" << *entry.uuid << "\n";
          }
        } else {
          r = sd_bus_message_skip(reply, "v");
          if (r < 0) die("skip variant", r);
        }

        r = sd_bus_message_exit_container(reply); // end {sv}
        if (r < 0) die("exit {sv}", r);
      }

      r = sd_bus_message_exit_container(reply); // end a{sv}
      if (r < 0) die("exit a{sv}", r);

      out.push_back(std::move(entry));

      r = sd_bus_message_exit_container(reply); // end (sa{sv})
      if (r < 0) die("exit (sa{sv})", r);
    }

    r = sd_bus_message_exit_container(reply); // end a of interfaces
    if (r < 0) die("exit a of interfaces", r);

    r = sd_bus_message_exit_container(reply); // end dict entry
    if (r < 0) die("exit dict entry", r);
  }

  if (r < 0) die("iterate managed objects", r);

  r = sd_bus_message_exit_container(reply); // end outer array
  if (r < 0) die("exit outer array", r);

  sd_bus_message_unref(reply);
  DBG << "[dbg] GetManagedObjects -> " << out.size() << " iface entries\n";
  return out;
}

struct FoundDev {
  std::string path;
  std::string name;
};

std::optional<FoundDev> find_any_device_by_names(sd_bus* bus,
                                                 const std::vector<std::string_view>& names) {
  auto objs = get_managed_objects(bus);
  for (const auto& e : objs) {
    if (e.interface == kDevice1 && e.name) {
      for (auto n : names) {
        if (*e.name == n) return FoundDev{e.path, std::string(n)};
      }
    }
  }
  return std::nullopt;
}

int call_void(sd_bus* bus, const std::string& path,
              std::string_view iface, std::string_view method) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message* reply = nullptr;
  int r = sd_bus_call_method(bus,
    std::string(kBluezService).c_str(),
    path.c_str(),
    std::string(iface).c_str(),
    std::string(method).data(),
    &error, &reply, "");
  if (r < 0) {
    std::cerr << "[err] D-Bus: " << (error.name ? error.name : "unknown")
              << " - " << (error.message ? error.message : "") << "\n";
  } else {
    DBG << "[dbg] call " << iface << "." << method << " on " << path << " -> ok\n";
  }
  sd_bus_error_free(&error);
  sd_bus_message_unref(reply);
  return r;
}

bool get_device_connected(sd_bus* bus, const std::string& dev_path) {
  sd_bus_message* m = nullptr;
  sd_bus_message* reply = nullptr;

  int r = sd_bus_message_new_method_call(
    bus, &m, std::string(kBluezService).c_str(),
    dev_path.c_str(),
    std::string(kProps).c_str(),
    "Get");
  if (r < 0) die("message_new(Get Connected)", r);

  r = sd_bus_message_append(m, "ss",
    std::string(kDevice1).c_str(),
    "Connected");
  if (r < 0) die("append Get args", r);

  r = sd_bus_call(bus, m, 0, nullptr, &reply);
  sd_bus_message_unref(m);
  if (r < 0) return false;

  r = sd_bus_message_enter_container(reply, 'v', "b");
  if (r < 0) die("enter v(b)", r);
  int connected = 0;
  r = sd_bus_message_read_basic(reply, 'b', &connected);
  if (r < 0) die("read bool", r);
  r = sd_bus_message_exit_container(reply);
  if (r < 0) die("exit v(b)", r);

  sd_bus_message_unref(reply);
  return connected;
}

std::optional<std::string> find_char_by_uuid(sd_bus* bus,
                                             const std::string& dev_path,
                                             std::string_view uuid) {
  std::string needle = to_lower_uuid(std::string(uuid));
  auto objs = get_managed_objects(bus);
  for (const auto& e : objs) {
    if (e.interface == kGattChar1) {
      if (e.path.rfind(dev_path, 0) == 0 && e.uuid && *e.uuid == needle) {
        DBG << "[dbg] Found characteristic " << needle << " at: " << e.path << "\n";
        return e.path;
      }
    }
  }
  return std::nullopt;
}

int start_notify(sd_bus* bus, const std::string& char_path) {
  DBG << "[dbg] Starting notifications on: " << char_path << "\n";
  return call_void(bus, char_path, kGattChar1, "StartNotify");
}

std::optional<bool> get_char_notifying(sd_bus* bus, const std::string& char_path) {
  sd_bus_message* m = nullptr;
  sd_bus_message* reply = nullptr;

  int r = sd_bus_message_new_method_call(
    bus, &m, std::string(kBluezService).c_str(),
    char_path.c_str(),
    std::string(kProps).c_str(),
    "Get");
  if (r < 0) return std::nullopt;

  r = sd_bus_message_append(m, "ss",
    std::string(kGattChar1).c_str(),
    "Notifying");
  if (r < 0) { sd_bus_message_unref(m); return std::nullopt; }

  r = sd_bus_call(bus, m, 0, nullptr, &reply);
  sd_bus_message_unref(m);
  if (r < 0) return std::nullopt;

  r = sd_bus_message_enter_container(reply, 'v', "b");
  if (r < 0) { sd_bus_message_unref(reply); return std::nullopt; }

  int notifying = 0;
  r = sd_bus_message_read_basic(reply, 'b', &notifying);
  if (r < 0) { sd_bus_message_exit_container(reply); sd_bus_message_unref(reply); return std::nullopt; }

  sd_bus_message_exit_container(reply);
  sd_bus_message_unref(reply);
  return notifying != 0;
}

bool path_has_interface(sd_bus* bus, const std::string& path, std::string_view iface) {
  auto objs = get_managed_objects(bus);
  for (const auto& e : objs) {
    if (e.path == path && e.interface == iface) return true;
  }
  return false;
}

// --- ReadValue helper (returns ay) ---
std::optional<std::vector<uint8_t>> read_char_value(sd_bus* bus, const std::string& char_path) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message* m = nullptr;
  sd_bus_message* reply = nullptr;

  int r = sd_bus_message_new_method_call(
    bus, &m, std::string(kBluezService).c_str(),
    char_path.c_str(),
    std::string(kGattChar1).c_str(),
    "ReadValue");
  if (r < 0) return std::nullopt;

  // Append empty options dict a{sv}
  r = sd_bus_message_open_container(m, 'a', "{sv}");
  if (r < 0) { sd_bus_message_unref(m); return std::nullopt; }
  r = sd_bus_message_close_container(m);
  if (r < 0) { sd_bus_message_unref(m); return std::nullopt; }

  r = sd_bus_call(bus, m, 0, &error, &reply);
  sd_bus_message_unref(m);
  if (r < 0) {
    std::cerr << "[warn] ReadValue failed on " << char_path << ": "
              << (error.message ? error.message : "") << "\n";
    sd_bus_error_free(&error);
    return std::nullopt;
  }
  sd_bus_error_free(&error);

  // Return is ay
  r = sd_bus_message_enter_container(reply, 'a', "y");
  if (r < 0) { sd_bus_message_unref(reply); return std::nullopt; }
  std::vector<uint8_t> out;
  for (;;) {
    uint8_t b = 0;
    int rr = sd_bus_message_read_basic(reply, 'y', &b);
    if (rr <= 0) break;
    out.push_back(b);
  }
  sd_bus_message_exit_container(reply);
  sd_bus_message_unref(reply);
  return out;
}

// PropertiesChanged handler: HR Measurement parser -> BPM + RR
struct NotifyCtx {
  std::ofstream out;     // BPM stream
  std::ofstream rr_out;  // RR intervals stream
  std::string char_path;
  std::string model_prefix; // "polarh9" or "polarh10"
};

int props_changed_cb(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
  (void)ret_error;
  NotifyCtx* ctx = static_cast<NotifyCtx*>(userdata);

  const char* interface = nullptr;
  int r = sd_bus_message_read(m, "s", &interface);
  if (r < 0) return 0;

  if (!interface || std::string_view(interface) != kGattChar1) {
    return 0; // not our interface
  }

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return 0;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char* prop = nullptr;
    r = sd_bus_message_read(m, "s", &prop);
    if (r < 0) break;

    char vt;
    const char* vtsig = nullptr;
    r = sd_bus_message_peek_type(m, &vt, &vtsig);
    if (r < 0) break;

    if (prop && std::strcmp(prop, "Value") == 0 && vtsig && std::strcmp(vtsig, "ay") == 0) {
      r = sd_bus_message_enter_container(m, 'v', "ay");
      if (r < 0) break;
      r = sd_bus_message_enter_container(m, 'a', "y");
      if (r < 0) break;

      std::vector<uint8_t> bytes;
      for (;;) {
        uint8_t b = 0;
        int rr = sd_bus_message_read_basic(m, 'y', &b);
        if (rr <= 0) break;
        bytes.push_back(b);
      }
      sd_bus_message_exit_container(m); // end array
      sd_bus_message_exit_container(m); // end variant

      uint64_t t = now_ms();

      // ---- Parse Heart Rate Measurement (Bluetooth SIG) ----
      int bpm = -1;
      std::vector<int> rr_ms; // milliseconds

      if (!bytes.empty()) {
        uint8_t flags = bytes[0];
        size_t idx = 1;

        bool hr_16bit     = (flags & 0x01) != 0;
        bool ee_present   = (flags & 0x08) != 0;
        bool rr_present   = (flags & 0x10) != 0;

        if (hr_16bit) {
          if (bytes.size() >= idx + 2) {
            bpm = (int)bytes[idx] | ((int)bytes[idx + 1] << 8);
            idx += 2;
          }
        } else {
          if (bytes.size() >= idx + 1) {
            bpm = bytes[idx];
            idx += 1;
          }
        }

        // Skip Energy Expended if present (uint16)
        if (ee_present && bytes.size() >= idx + 2) {
          idx += 2;
        }

        // RR-Intervals (uint16 each, units = 1/1024 s)
        if (rr_present) {
          while (bytes.size() >= idx + 2) {
            uint16_t rr1024 = (uint16_t)bytes[idx] | ((uint16_t)bytes[idx + 1] << 8);
            idx += 2;
            int rr_val_ms = (int)((rr1024 * 1000ULL + 512ULL) / 1024ULL);
            rr_ms.push_back(rr_val_ms);
          }
        }

        DBG << "[dbg] HRM parse: flags=0x" << std::hex << (int)flags << std::dec
            << " hr16=" << hr_16bit
            << " ee=" << ee_present
            << " rr=" << rr_present
            << " bpm=" << bpm
            << " rr_count=" << rr_ms.size()
            << " bytes=[" << to_hex(bytes) << "]\n";
      } else {
        DBG << "[dbg] HRM parse: empty payload\n";
      }

      // ---- Write outputs ----
      if (ctx && ctx->out.good()) {
        ctx->out << t << "," << bpm << "\n";
        ctx->out.flush();
      }
      if (ctx && ctx->rr_out.good() && !rr_ms.empty()) {
        for (int rrval : rr_ms) {
          ctx->rr_out << t << "," << rrval << "\n";
        }
        ctx->rr_out.flush();
      }
    } else {
      r = sd_bus_message_skip(m, "v");
      if (r < 0) break;
    }
    sd_bus_message_exit_container(m); // end dict entry
  }
  sd_bus_message_exit_container(m); // end dict

  sd_bus_message_skip(m, "as"); // invalidated
  return 0;
}

// Generic PropertiesChanged handler: logs ay either as battery percent or hex.
struct GenericNotifyCtx {
  std::ofstream out;
  std::string char_path;
  std::string uuid;
  bool is_battery{false};
  std::string label; // for debug
};

int generic_props_changed_cb(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
  (void)ret_error;
  auto* ctx = static_cast<GenericNotifyCtx*>(userdata);

  const char* interface = nullptr;
  int r = sd_bus_message_read(m, "s", &interface);
  if (r < 0) return 0;
  if (!interface || std::string_view(interface) != kGattChar1) return 0;

  r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return 0;

  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char* prop = nullptr;
    r = sd_bus_message_read(m, "s", &prop);
    if (r < 0) break;

    char vt;
    const char* vtsig = nullptr;
    r = sd_bus_message_peek_type(m, &vt, &vtsig);
    if (r < 0) break;

    if (prop && std::strcmp(prop, "Value") == 0 && vtsig && std::strcmp(vtsig, "ay") == 0) {
      r = sd_bus_message_enter_container(m, 'v', "ay");
      if (r < 0) break;
      r = sd_bus_message_enter_container(m, 'a', "y");
      if (r < 0) break;

      std::vector<uint8_t> bytes;
      for (;;) {
        uint8_t b = 0;
        int rr = sd_bus_message_read_basic(m, 'y', &b);
        if (rr <= 0) break;
        bytes.push_back(b);
      }
      sd_bus_message_exit_container(m);
      sd_bus_message_exit_container(m);

      uint64_t t = now_ms();
      if (ctx && ctx->out.good()) {
        if (ctx->is_battery && !bytes.empty()) {
          int pct = bytes[0];
          ctx->out << t << "," << pct << "\n";
          ctx->out.flush();
          DBG << "[dbg] battery notify: " << pct << "% (" << ctx->char_path << ")\n";
        } else {
          ctx->out << t << "," << to_hex(bytes) << "\n";
          ctx->out.flush();
          DBG << "[dbg] generic notify (" << ctx->label << " " << ctx->uuid << "): "
              << to_hex(bytes) << "\n";
        }
      }
    } else {
      r = sd_bus_message_skip(m, "v");
      if (r < 0) break;
    }
    sd_bus_message_exit_container(m);
  }
  sd_bus_message_exit_container(m);
  sd_bus_message_skip(m, "as");
  return 0;
}

std::optional<std::string> reacquire_device(sd_bus* bus,
                                            const std::vector<std::string_view>& names) {
  std::cerr << "[info] Reacquiring device by name via discovery...\n";
  int r = call_void(bus, std::string(kAdapterPath), kAdapter1, "StartDiscovery");
  if (r < 0) std::cerr << "[warn] StartDiscovery failed while reacquiring\n";

  auto deadline = std::chrono::steady_clock::now() + 15s;
  std::optional<FoundDev> dev;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(2s);
    dev = find_any_device_by_names(bus, names);
    if (dev) break;
    DBG << "[dbg] reacquire: still scanning...\n";
  }
  call_void(bus, std::string(kAdapterPath), kAdapter1, "StopDiscovery");
  if (!dev) std::cerr << "[warn] Reacquire failed: device still not found\n";
  return dev ? std::optional<std::string>(dev->path) : std::nullopt;
}

// Ensure (re)connection and (re)subscription to notifications (HR char).
void ensure_connected_and_notifying(sd_bus* bus,
                                    std::string& dev_path,
                                    std::string& ch_path,
                                    sd_bus_slot*& slot,
                                    NotifyCtx& ctx,
                                    const std::vector<std::string_view>& names) {
  DBG << "[dbg] maintenance tick: ensuring connection and HR notifications\n";
  if (dev_path.empty() || !path_has_interface(bus, dev_path, kDevice1)) {
    std::cerr << "[warn] Device path missing; attempting reacquire...\n";
    auto np = reacquire_device(bus, names);
    if (np) {
      dev_path = *np;
      std::cerr << "[info] Reacquired device path: " << dev_path << "\n";
    } else {
      std::cerr << "[warn] Device still not present.\n";
      return;
    }
  }

  if (!get_device_connected(bus, dev_path)) {
    std::cerr << "[info] Connecting (maintenance)...\n";
    int r = call_void(bus, dev_path, kDevice1, "Connect");
    if (r < 0) { std::cerr << "[warn] Connect() failed in maintenance.\n"; return; }

    auto deadline = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (get_device_connected(bus, dev_path)) break;
      std::this_thread::sleep_for(500ms);
    }
    if (!get_device_connected(bus, dev_path)) {
      std::cerr << "[warn] Connect timeout in maintenance.\n";
      return;
    }
    std::cerr << "[info] Connected (maintenance).\n";
  }

  if (ch_path.empty() || !path_has_interface(bus, ch_path, kGattChar1)) {
    auto np = find_char_by_uuid(bus, dev_path, kHRCharUUID);
    if (!np) {
      std::cerr << "[warn] HR characteristic not present yet.\n";
      return;
    }
    if (np && *np != ch_path) {
      std::cerr << "[info] HR characteristic path changed -> " << *np << "\n";
      if (slot) { sd_bus_slot_unref(slot); slot = nullptr; }
      ch_path = *np;
      ctx.char_path = ch_path;
      std::string match =
        "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" + ch_path + "'";
      int r = sd_bus_add_match(bus, &slot, match.c_str(), props_changed_cb, &ctx);
      if (r < 0) die("sd_bus_add_match(PropertiesChanged re-add)", r);
      DBG << "[dbg] Reinstalled HR Value match for " << ch_path << "\n";
    }
  }

  if (!ch_path.empty()) {
    auto n = get_char_notifying(bus, ch_path);
    if (!n.has_value() || !*n) {
      std::cerr << "[info] Notifying=false (or unknown). Calling StartNotify...\n";
      int r = start_notify(bus, ch_path);
      if (r < 0) {
        std::cerr << "[warn] StartNotify failed in maintenance.\n";
      } else {
        std::cerr << "[info] StartNotify ok (maintenance).\n";
      }
    } else {
      DBG << "[dbg] Notifying=true\n";
    }
  }
}

// For H10: set up additional notifying characteristics (battery + generic).
struct GenericSet {
  std::set<std::string> installed_paths;
  std::vector<std::unique_ptr<GenericNotifyCtx>> contexts;
  std::vector<sd_bus_slot*> slots;
  std::string model_prefix;
};

void setup_additional_characteristics(sd_bus* bus,
                                      const std::string& dev_path,
                                      const std::string& hr_path,
                                      GenericSet& gen) {
  auto objs = get_managed_objects(bus);
  for (const auto& e : objs) {
    if (e.interface != kGattChar1) continue;
    if (e.path.rfind(dev_path, 0) != 0) continue;
    if (e.path == hr_path) continue;
    if (!e.uuid) continue;

    std::string uuid = *e.uuid;
    if (gen.installed_paths.count(e.path)) continue;

    bool is_batt = (uuid == to_lower_uuid(std::string(kBattUUID)));
    std::string outfile;
    std::string label = "char";
    if (is_batt) {
      outfile = out_suffix_file_path(gen.model_prefix, "battery");
      label = "battery";
    } else {
      // generic filename: <prefix>_<uuid8>
      std::string uuid8 = uuid.size() >= 8 ? uuid.substr(0, 8) : uuid;
      outfile = out_suffix_file_path(gen.model_prefix, uuid8);
    }
    ensure_parent_dir(outfile);

    auto ctx = std::make_unique<GenericNotifyCtx>();
    ctx->char_path = e.path;
    ctx->uuid = uuid;
    ctx->is_battery = is_batt;
    ctx->label = label;
    ctx->out.open(outfile, std::ios::app);
    if (!ctx->out) {
      std::cerr << "[warn] cannot open generic output: " << outfile << "\n";
      continue;
    }

    // Try to start notify (it's ok if it fails for read-only chars)
    int r = start_notify(bus, e.path);
    if (r < 0) {
      DBG << "[dbg] StartNotify failed (non-notify char?): " << e.path << "\n";
    }

    sd_bus_slot* slot = nullptr;
    std::string match =
      "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',"
      "member='PropertiesChanged',path='" + e.path + "'";
    int rr = sd_bus_add_match(bus, &slot, match.c_str(), generic_props_changed_cb, ctx.get());
    if (rr < 0) {
      std::cerr << "[warn] add match failed for " << e.path << "\n";
      continue;
    }

    gen.installed_paths.insert(e.path);
    gen.contexts.push_back(std::move(ctx));
    gen.slots.push_back(slot);
    std::cerr << "[info] Installed generic notifier (" << label << ") at " << e.path
              << " uuid=" << uuid << "\n";
  }
}

// One-shot capture of some readable fields for H10 device-info/bodysensorloc.
void capture_static_info(sd_bus* bus,
                         const std::string& dev_path,
                         const std::string& prefix) {
  std::string infofile = out_suffix_file_path(prefix, "device_info");
  ensure_parent_dir(infofile);
  std::ofstream out(infofile, std::ios::app);
  if (!out) { std::cerr << "[warn] cannot open device_info output\n"; return; }
  auto write_string_field = [&](std::string_view uuid, const char* key){
    auto path = find_char_by_uuid(bus, dev_path, uuid);
    if (!path) return;
    auto bytes = read_char_value(bus, *path);
    if (!bytes) return;
    std::string val(bytes->begin(), bytes->end());
    uint64_t t = now_ms();
    out << t << "," << key << "=" << val << "\n";
    DBG << "[dbg] info " << key << "=" << val << "\n";
  };
  auto write_uint8_field = [&](std::string_view uuid, const char* key){
    auto path = find_char_by_uuid(bus, dev_path, uuid);
    if (!path) return;
    auto bytes = read_char_value(bus, *path);
    if (!bytes || bytes->empty()) return;
    uint64_t t = now_ms();
    out << t << "," << key << "=" << (int)(*bytes)[0] << "\n";
    DBG << "[dbg] info " << key << "=" << (int)(*bytes)[0] << "\n";
  };

  write_uint8_field(kBodyLocUUID, "body_sensor_location");
  write_string_field(kManufNameUUID, "manufacturer_name");
  write_string_field(kModelNumUUID,  "model_number");
  write_string_field(kHwRevUUID,     "hardware_rev");
  write_string_field(kFwRevUUID,     "firmware_rev");
  write_string_field(kSwRevUUID,     "software_rev");
}

int run_impl() {
  DBG << "[dbg] run_impl(): starting\n";
  Bus bus;

  // 1) Try to find target device (prefer H10 if both appear)
  std::vector<std::string_view> names = {kTargetNameH10, kTargetNameH9};
  auto dev = find_any_device_by_names(bus, names);

  // 2) If not found, StartDiscovery and poll until we see it
  if (!dev) {
    std::cerr << "[info] Starting discovery on " << std::string(kAdapterPath) << " ...\n";
    int r = call_void(bus, std::string(kAdapterPath), kAdapter1, "StartDiscovery");
    if (r < 0) die("StartDiscovery", r);

    auto deadline = std::chrono::steady_clock::now() + 90s; // wait up to 90s
    int iter = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(2s);
      dev = find_any_device_by_names(bus, names);
      if (dev) break;
      DBG << "[dbg] scan iteration " << (++iter) << " ... not yet found\n";
    }
    call_void(bus, std::string(kAdapterPath), kAdapter1, "StopDiscovery");

    if (!dev) {
      std::cerr << "[err] Device not found after scan.\n";
      return EXIT_FAILURE;
    }
  }

  std::cerr << "[info] Found device: " << dev->name << " path: " << dev->path << "\n";
  const std::string model_prefix = prefix_for_name(dev->name);

  // 3) Connect (no-op if already connected)
  if (!get_device_connected(bus, dev->path)) {
    std::cerr << "[info] Connecting...\n";
    int r = call_void(bus, dev->path, kDevice1, "Connect");
    if (r < 0) die("Connect", r);

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

  // --- Open outputs (HR + RR) ---
  std::string outfile   = out_file_path(model_prefix);
  std::string out_rrfile= out_rr_file_path(model_prefix);
  ensure_parent_dir(outfile);
  ensure_parent_dir(out_rrfile);
  std::ofstream out(outfile, std::ios::app);
  if (!out) { std::cerr << "[err] Failed to open output file: " << outfile << "\n"; return EXIT_FAILURE; }
  std::ofstream rr_out(out_rrfile, std::ios::app);
  if (!rr_out) { std::cerr << "[err] Failed to open RR output file: " << out_rrfile << "\n"; return EXIT_FAILURE; }
  std::cerr << "[info] Output -> " << outfile << "\n";
  std::cerr << "[info] RR Output -> " << out_rrfile << "\n";

  // 4) Find Heart Rate Measurement characteristic
  auto ch_path = find_char_by_uuid(bus, dev->path, kHRCharUUID);
  if (!ch_path) {
    std::cerr << "[err] Heart Rate Measurement characteristic not found.\n";
    return EXIT_FAILURE;
  }
  std::cerr << "[info] Heart Rate characteristic: " << *ch_path << "\n";

  // 5) StartNotify (enables CCCD internally)
  int r = start_notify(bus, *ch_path);
  if (r < 0) die("StartNotify", r);

  // 6) Subscribe to PropertiesChanged on that HR path
  NotifyCtx ctx{std::move(out), std::move(rr_out), *ch_path, model_prefix};
  std::string match =
    "type='signal',"
    "sender='org.bluez',"
    "interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',"
    "path='" + *ch_path + "'";
  DBG << "[dbg] Installing HR D-Bus match: " << match << "\n";

  sd_bus_slot* slot = nullptr;
  r = sd_bus_add_match(bus, &slot, match.c_str(), props_changed_cb, &ctx);
  if (r < 0) die("sd_bus_add_match(PropertiesChanged)", r);

  // 7) If H10, set up additional streams and capture static info.
  GenericSet gset;
  gset.model_prefix = model_prefix;
  if (model_prefix == "polarh10") {
    capture_static_info(bus, dev->path, model_prefix);
    setup_additional_characteristics(bus, dev->path, *ch_path, gset);
  }

  std::cerr << "[info] Listening for notifications (Ctrl+C to quit)...\n";
  // 8) Simple event loop with maintenance (0.5s tick)
  for (;;) {
    r = sd_bus_process(bus, nullptr);
    if (r < 0) die("sd_bus_process", r);
    if (r == 0) {
      ensure_connected_and_notifying(bus, dev->path, *ch_path, slot, ctx, names);
      if (model_prefix == "polarh10") {
        // idempotent: only installs new paths if any (e.g., after reconnection)
        setup_additional_characteristics(bus, dev->path, *ch_path, gset);

        // Battery polling fallback: if battery char exists but doesn't notify,
        // poll it lightly (~every 60s).
        static auto last_batt_poll = std::chrono::steady_clock::now() - 60s;
        if (std::chrono::steady_clock::now() - last_batt_poll >= 60s) {
          auto batt = find_char_by_uuid(bus, dev->path, kBattUUID);
          if (batt) {
            auto bytes = read_char_value(bus, *batt);
            if (bytes && !bytes->empty()) {
              std::string battfile = out_suffix_file_path(model_prefix, "battery");
              ensure_parent_dir(battfile);
              std::ofstream bout(battfile, std::ios::app);
              if (bout) {
                bout << now_ms() << "," << (int)(*bytes)[0] << "\n";
              }
              DBG << "[dbg] battery poll: " << (int)(*bytes)[0] << "%\n";
            }
          }
          last_batt_poll = std::chrono::steady_clock::now();
        }
      }
      r = sd_bus_wait(bus, poll_time);
      if (r < 0) die("sd_bus_wait", r);
    }
  }
}

} // anonymous namespace

int main(int argc, char** argv) {
  // Parse flags: -d or --debug enables [dbg] messages
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
#endif

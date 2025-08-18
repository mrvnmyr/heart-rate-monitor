export module polarh9;

// Global module fragment for legacy includes
module;

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

export namespace polarh9 {

using namespace std::chrono_literals;

static constexpr std::string_view kBluezService = "org.bluez";
static constexpr std::string_view kObjManager = "org.freedesktop.DBus.ObjectManager";
static constexpr std::string_view kProps = "org.freedesktop.DBus.Properties";
static constexpr std::string_view kAdapter1 = "org.bluez.Adapter1";
static constexpr std::string_view kDevice1 = "org.bluez.Device1";
static constexpr std::string_view kGattChar1 = "org.bluez.GattCharacteristic1";

static constexpr std::string_view kAdapterPath = "/org/bluez/hci0"; // simple + common
static constexpr std::string_view kTargetName = "Polar H9 EA190E24";
static constexpr std::string_view kHRCharUUID = "00002a37-0000-1000-8000-00805f9b34fb";

[[noreturn]] static void die(const char* msg, int err) {
  std::cerr << "Error: " << msg << " (" << -err << "): " << strerror(-err) << "\n";
  std::exit(EXIT_FAILURE);
}

static std::string getenv_or(const char* key, const char* defv) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string(defv);
}

static std::string out_file_path() {
  std::string home = getenv_or("HOME", ".");
  std::filesystem::path p = std::filesystem::path(home) / ".cache" / "polarh9";
  return p.string();
}

static void ensure_parent_dir(const std::string& file) {
  std::filesystem::path p(file);
  std::filesystem::create_directories(p.parent_path());
}

static std::string to_hex(const std::vector<uint8_t>& v) {
  std::ostringstream oss;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) oss << ' ';
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)v[i];
  }
  return oss.str();
}

static uint64_t now_ms() {
  using clock = std::chrono::time_point<std::chrono::system_clock>;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch());
  return (uint64_t)ms.count();
}

struct Bus {
  sd_bus* bus{};
  Bus() {
#ifdef __ANDROID__
    // Android build is a stub (no BlueZ); gracefully inform and exit.
    std::cerr << "Android build: BlueZ D-Bus not available. "
                 "This binary is a build-time stub.\n";
    std::exit(0);
#else
    int r = sd_bus_open_system(&bus);
    if (r < 0) die("sd_bus_open_system", r);
#endif
  }
  ~Bus() { if (bus) sd_bus_unref(bus); }
  operator sd_bus*() const { return bus; }
};

// Call ObjectManager.GetManagedObjects and iterate to find:
//  - a Device1 whose "Name" equals kTargetName  -> return its object path
// Also used later to find GattCharacteristic1 with target UUID under a device path.
struct ManagedObjectsEntry {
  std::string path;
  std::string interface;
  // Only the fields we care about
  std::optional<std::string> name;
  std::optional<std::string> uuid;
};

static std::vector<ManagedObjectsEntry> get_managed_objects(sd_bus* bus) {
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

    r = sd_bus_message_enter_container(reply, 'a', "sa{sv}");
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

        // We only care about a couple of string properties: "Name" and "UUID"
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
          } else if (prop && std::strcmp(prop, "UUID") == 0) {
            std::string u = sval ? std::string(sval) : std::string();
            // Normalize lowercase
            std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c){return (char)std::tolower(c);});
            entry.uuid = std::move(u);
          } else {
            // ignore other string properties
          }
        } else {
          // Skip other property types
          r = sd_bus_message_skip(reply, "v");
          if (r < 0) die("skip variant", r);
        }

        r = sd_bus_message_exit_container(reply); // end dict entry
        if (r < 0) die("exit {sv}", r);
      }

      r = sd_bus_message_exit_container(reply); // end a{sv}
      if (r < 0) die("exit a{sv}", r);

      // We push an entry per interface so we can filter later
      out.push_back(std::move(entry));

      r = sd_bus_message_exit_container(reply); // end dict entry (sa{sv})
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
  return out;
}

static std::optional<std::string> find_device_by_name(sd_bus* bus, std::string_view name) {
  auto objs = get_managed_objects(bus);
  for (const auto& e : objs) {
    if (e.interface == kDevice1 && e.name && *e.name == name) {
      return e.path;
    }
  }
  return std::nullopt;
}

static int call_void(sd_bus* bus, const std::string& path,
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
    std::cerr << "D-Bus error: " << (error.name ? error.name : "unknown")
              << " - " << (error.message ? error.message : "") << "\n";
  }
  sd_bus_error_free(&error);
  sd_bus_message_unref(reply);
  return r;
}

static bool get_device_connected(sd_bus* bus, const std::string& dev_path) {
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

  // reply: v
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

static std::optional<std::string> find_hr_char(sd_bus* bus, const std::string& dev_path) {
  auto objs = get_managed_objects(bus);
  for (const auto& e : objs) {
    if (e.interface == kGattChar1) {
      // only consider characteristics under this device subtree
      if (e.path.rfind(dev_path, 0) == 0 /* starts with */) {
        if (e.uuid) {
          std::string u = *e.uuid;
          std::string needle(kHRCharUUID);
          std::transform(needle.begin(), needle.end(), needle.begin(),
                         [](unsigned char c){return (char)std::tolower(c);});
          if (u == needle) {
            return e.path;
          }
        }
      }
    }
  }
  return std::nullopt;
}

static int start_notify(sd_bus* bus, const std::string& char_path) {
  return call_void(bus, char_path, kGattChar1, "StartNotify");
}

// PropertiesChanged handler: extract "Value" (ay) updates and write a line to file.
struct NotifyCtx {
  std::ofstream out;
  std::string char_path;
};

static int props_changed_cb(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
  (void)ret_error;
  NotifyCtx* ctx = static_cast<NotifyCtx*>(userdata);

  const char* interface = nullptr;
  int r = sd_bus_message_read(m, "s", &interface);
  if (r < 0) return 0;

  if (!interface || std::string_view(interface) != kGattChar1) {
    return 0; // not our interface
  }

  // changed properties dict
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

      // Parse Heart Rate Measurement (spec)
      int bpm = -1;
      if (bytes.size() >= 2) {
        uint8_t flags = bytes[0];
        bool sixteen = (flags & 0x01) != 0;
        if (!sixteen && bytes.size() >= 2) {
          bpm = bytes[1];
        } else if (sixteen && bytes.size() >= 3) {
          bpm = (int)bytes[1] | ((int)bytes[2] << 8);
        }
      }

      uint64_t t = now_ms();
      if (ctx && ctx->out.good()) {
        // line: epoch_ms,bpm,hex
        ctx->out << t << "," << bpm << "," << to_hex(bytes) << "\n";
        ctx->out.flush();
      }
    } else {
      // Skip other variants
      r = sd_bus_message_skip(m, "v");
      if (r < 0) break;
    }
    sd_bus_message_exit_container(m); // end dict entry
  }
  sd_bus_message_exit_container(m); // end dict

  // invalidated props array (we can skip)
  sd_bus_message_skip(m, "as");
  return 0;
}

static void run() {
  Bus bus;

  std::string outfile = out_file_path();
  ensure_parent_dir(outfile);
  std::ofstream out(outfile, std::ios::app);
  if (!out) {
    std::cerr << "Failed to open output file: " << outfile << "\n";
    std::exit(EXIT_FAILURE);
  }

  std::cerr << "Output -> " << outfile << "\n";

  // 1) Try to find device right away
  std::optional<std::string> dev_path = find_device_by_name(bus, kTargetName);

  // 2) If not found, StartDiscovery and poll until we see it
  if (!dev_path) {
    std::cerr << "Starting discovery on " << std::string(kAdapterPath) << " ...\n";
    int r = call_void(bus, std::string(kAdapterPath), kAdapter1, "StartDiscovery");
    if (r < 0) die("StartDiscovery", r);

    auto deadline = std::chrono::steady_clock::now() + 90s; // wait up to 90s
    while (std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(2s);
      dev_path = find_device_by_name(bus, kTargetName);
      if (dev_path) break;
      std::cerr << "Scanning for \"" << kTargetName << "\"...\n";
    }

    // Stop discovery (even if not found)
    call_void(bus, std::string(kAdapterPath), kAdapter1, "StopDiscovery");

    if (!dev_path) {
      std::cerr << "Device not found.\n";
      std::exit(EXIT_FAILURE);
    }
  }

  std::cerr << "Found device path: " << *dev_path << "\n";

  // 3) Connect (no-op if already connected)
  if (!get_device_connected(bus, *dev_path)) {
    std::cerr << "Connecting...\n";
    int r = call_void(bus, *dev_path, kDevice1, "Connect");
    if (r < 0) die("Connect", r);

    // Wait until Connected=true
    auto deadline = std::chrono::steady_clock::now() + 20s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (get_device_connected(bus, *dev_path)) break;
      std::this_thread::sleep_for(500ms);
    }
    if (!get_device_connected(bus, *dev_path)) {
      std::cerr << "Failed to connect (timeout).\n";
      std::exit(EXIT_FAILURE);
    }
  }
  std::cerr << "Connected.\n";

  // 4) Find Heart Rate Measurement characteristic
  auto ch_path = find_hr_char(bus, *dev_path);
  if (!ch_path) {
    std::cerr << "Heart Rate Measurement characteristic not found.\n";
    std::exit(EXIT_FAILURE);
  }
  std::cerr << "Heart Rate characteristic: " << *ch_path << "\n";

  // 5) StartNotify (enables CCCD internally)
  int r = start_notify(bus, *ch_path);
  if (r < 0) die("StartNotify", r);

  // 6) Subscribe to PropertiesChanged on that path
  NotifyCtx ctx{std::move(out), *ch_path};
  std::string match =
    "type='signal',"
    "sender='org.bluez',"
    "interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',"
    "path='" + *ch_path + "'";

  sd_bus_slot* slot = nullptr;
  r = sd_bus_add_match(bus, &slot, match.c_str(), props_changed_cb, &ctx);
  if (r < 0) die("sd_bus_add_match(PropertiesChanged)", r);

  std::cerr << "Listening for notifications (Ctrl+C to quit)...\n";
  // 7) Simple event loop
  for (;;) {
    r = sd_bus_process(bus, nullptr);
    if (r < 0) die("sd_bus_process", r);
    if (r == 0) {
      r = sd_bus_wait(bus, (uint64_t)-1);
      if (r < 0) die("sd_bus_wait", r);
    }
  }
}

} // namespace polarh9

// Define main within the module unit (no separate headers needed)
int main() {
  try {
    polarh9::run();
  } catch (const std::exception& e) {
    std::cerr << "Unhandled exception: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

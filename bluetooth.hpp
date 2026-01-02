#pragma once
#include <systemd/sd-bus.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "debug.hpp"

struct Bus {
  sd_bus* bus{};
  Bus();
  ~Bus();
  operator sd_bus*() const { return bus; }
};

struct FoundDev {
  std::string path;
  std::string name;
};

// Core BlueZ helpers
std::optional<FoundDev> find_any_device_by_names(sd_bus* bus,
                                                 const std::vector<std::string_view>& names);
int call_void(sd_bus* bus, const std::string& path,
              std::string_view iface, std::string_view method,
              std::string* out_err_name = nullptr,
              std::string* out_err_msg = nullptr);
bool get_device_connected(sd_bus* bus, const std::string& dev_path);
std::optional<std::string> find_char_by_uuid(sd_bus* bus,
                                             const std::string& dev_path,
                                             std::string_view uuid);
int start_notify(sd_bus* bus, const std::string& char_path);
std::optional<bool> get_char_notifying(sd_bus* bus, const std::string& char_path);
bool path_has_interface(sd_bus* bus, const std::string& path, std::string_view iface);

// Discovery helpers
int start_adapter_discovery(sd_bus* bus);
int stop_adapter_discovery(sd_bus* bus);

// Maintenance
std::optional<std::string> reacquire_device(sd_bus* bus,
                                            const std::vector<std::string_view>& names);
void ensure_connected_and_notifying(sd_bus* bus,
                                    std::string& dev_path,
                                    std::string& ch_path,
                                    sd_bus_slot*& slot,
                                    const std::vector<std::string_view>& names);

// HRM notification callback -> prints lines to stdout
int props_changed_cb(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);

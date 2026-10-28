// Language: C++20; file: device_inventory.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace open_driver {

struct InstalledDriver {
    std::string description;
    std::string provider;
    std::string version;
    std::string inf_path;
    std::string matching_device_id;
    std::uint32_t date_yyyymmdd{};
};

struct DeviceRecord {
    std::string instance_id;
    std::string class_name;
    std::string class_guid;
    std::string description;
    std::string friendly_name;
    std::string manufacturer;
    std::string location;
    std::vector<std::string> hardware_ids;
    std::vector<std::string> compatible_ids;
    InstalledDriver installed_driver;
    std::uint32_t status_flags{};
    std::uint32_t problem_code{};
    bool present{};
};

struct InventoryError {
    std::string operation;
    std::string device_instance_id;
    std::uint32_t windows_error{};
    std::string message;
};

struct DeviceInventoryOptions {
    bool present_only{true};
};

struct DeviceInventoryResult {
    std::vector<DeviceRecord> devices;
    std::vector<InventoryError> property_errors;
    std::optional<InventoryError> fatal_error;
};

[[nodiscard]] DeviceInventoryResult collect_device_inventory(
    const DeviceInventoryOptions& options = {});

[[nodiscard]] bool write_inventory_json(
    const DeviceInventoryResult& inventory,
    const std::string& output_path,
    std::string& error_message);

}  // namespace open_driver

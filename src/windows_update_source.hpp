// Language: C++20; file: windows_update_source.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "device_inventory.hpp"

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace open_driver {

struct WindowsUpdateDriverRecord {
    std::string update_id;
    std::int32_t revision_number{};
    std::string title;
    std::string description;
    std::string driver_class;
    std::string driver_hardware_id;
    std::string manufacturer;
    std::string model;
    std::string provider;
    std::uint32_t driver_date_yyyymmdd{};
    std::int32_t device_problem_number{};
    std::int32_t device_status{};
    bool downloaded{};
    bool hidden{};
    bool mandatory{};
    std::vector<std::string> matching_instance_ids;
    std::vector<std::string> matching_present_instance_ids;
};

struct WindowsUpdateError {
    std::string operation;
    std::int32_t hresult{};
    std::string message;
};

struct WindowsUpdateSearchResult {
    std::vector<WindowsUpdateDriverRecord> updates;
    std::optional<WindowsUpdateError> fatal_error;
    std::int32_t operation_result_code{};
    bool cancelled{};
};

[[nodiscard]] WindowsUpdateSearchResult search_windows_update_drivers(
    const std::vector<DeviceRecord>& inventory,
    bool online = true,
    std::stop_token stop = {});

}  // namespace open_driver

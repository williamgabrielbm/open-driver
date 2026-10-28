// Language: C++20; file: driver_health.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "device_inventory.hpp"
#include "windows_update_source.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace open_driver {

enum class DriverHealthStatus {
    confirmed_update,
    needs_oem_check,
    device_attention,
};

struct DriverHealthItem {
    std::string device_name;
    std::string class_name;
    std::string provider;
    std::string version;
    std::string inf_path;
    std::uint32_t driver_date_yyyymmdd{};
    DriverHealthStatus status{DriverHealthStatus::needs_oem_check};
    std::string plain_explanation;
    std::size_t related_devices{1};
};

struct DriverHealthReport {
    std::size_t present_devices{};
    std::size_t confirmed_updates{};
    std::size_t needs_oem_check{};
    std::size_t device_attention{};
    std::size_t ignored_windows_drivers{};
    std::size_t ignored_fixed_dates{};
    std::size_t ignored_virtual_devices{};
    std::vector<DriverHealthItem> items;
};

[[nodiscard]] DriverHealthReport assess_driver_health(
    const std::vector<DeviceRecord>& inventory,
    const std::vector<WindowsUpdateDriverRecord>& updates,
    std::uint32_t today_yyyymmdd);

[[nodiscard]] std::uint32_t local_date_yyyymmdd();

}  // namespace open_driver

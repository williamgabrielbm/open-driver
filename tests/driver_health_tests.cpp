// Language: C++20; file: driver_health_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "driver_health.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

open_driver::DeviceRecord device(
    std::string id, std::string class_name, std::string name,
    std::string provider, std::string version, std::string inf,
    std::uint32_t date, std::uint32_t problem = 0) {
    open_driver::DeviceRecord value;
    value.instance_id = std::move(id);
    value.class_name = std::move(class_name);
    value.description = std::move(name);
    value.present = true;
    value.problem_code = problem;
    value.installed_driver.provider = std::move(provider);
    value.installed_driver.version = std::move(version);
    value.installed_driver.inf_path = std::move(inf);
    value.installed_driver.date_yyyymmdd = date;
    return value;
}

}  // namespace

int main() {
    std::vector<open_driver::DeviceRecord> inventory{
        device("PCI\\OLD", "Net", "Old Ethernet", "Intel", "1.0", "old.inf", 20240101),
        device("PCI\\CURRENT", "Display", "Current GPU", "Intel", "2.0", "gpu.inf", 20261001),
        device("PCI\\SENTINEL", "System", "Chipset marker", "INTEL", "10.1", "chip.inf", 19680718),
        device("ROOT\\VPN", "Net", "Virtual VPN tunnel", "Vendor", "1", "vpn.inf", 20200101),
        device("PCI\\MS", "HDC", "Standard controller", "Microsoft", "10", "ms.inf", 20060621),
        device("PCI\\UPDATE", "Bluetooth", "Bluetooth", "Realtek", "3", "bt.inf", 20260101),
        device("PCI\\BROKEN", "Media", "Audio", "Vendor", "1", "audio.inf", 20261001, 28),
    };
    std::vector<open_driver::WindowsUpdateDriverRecord> updates{
        {.matching_present_instance_ids = {"pci\\update"}},
    };

    const auto report = open_driver::assess_driver_health(inventory, updates, 20261021);
    require(report.present_devices == 7, "all present devices must be counted");
    require(report.needs_oem_check == 1, "old vendor driver must require OEM check");
    require(report.confirmed_updates == 1, "matching WUA offer must be confirmed");
    require(report.device_attention == 1, "problem code must remain visible");
    require(report.ignored_fixed_dates == 1, "Intel fixed date must not look outdated");
    require(report.ignored_virtual_devices == 1, "virtual adapters must not pollute hardware results");
    require(report.ignored_windows_drivers == 1, "Windows inbox dates must be ignored");
    require(report.items.size() == 3, "only actionable or review items must be shown");
    require(report.items[0].status == open_driver::DriverHealthStatus::confirmed_update,
            "confirmed updates must sort first");
    std::cout << "driver_health_tests: passed\n";
    return 0;
}

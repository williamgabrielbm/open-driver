// Language: C++20; file: device_inventory_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "device_inventory.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const auto inventory = open_driver::collect_device_inventory({.present_only = true});
    require(!inventory.fatal_error.has_value(), "present-device enumeration must succeed");
    require(!inventory.devices.empty(), "a running Windows system must expose devices");

    std::size_t devices_with_identity = 0;
    std::size_t devices_with_hardware_ids = 0;
    for (const auto& device : inventory.devices) {
        devices_with_identity += !device.instance_id.empty();
        devices_with_hardware_ids += !device.hardware_ids.empty();
    }

    require(devices_with_identity == inventory.devices.size(),
            "every enumerated device must have an instance ID");
    require(devices_with_hardware_ids > 0,
            "the inventory must recover hardware IDs from at least one device");

    std::cout << "device_inventory_tests: devices=" << inventory.devices.size()
              << " property_errors=" << inventory.property_errors.size() << '\n';
    return 0;
}

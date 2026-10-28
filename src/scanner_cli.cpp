// Language: C++20; file: scanner_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "device_inventory.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bool present_only = true;
    std::string output_path = "device-inventory.json";

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--all") {
            present_only = false;
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else {
            std::cerr << "usage: open_driver_scan [--all] [--output <path>]\n";
            return 2;
        }
    }

    const auto inventory = open_driver::collect_device_inventory({
        .present_only = present_only,
    });
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->operation << ": "
                  << inventory.fatal_error->message << '\n';
        return 1;
    }

    std::string write_error;
    if (!open_driver::write_inventory_json(inventory, output_path, write_error)) {
        std::cerr << write_error << '\n';
        return 1;
    }

    std::cout << "devices=" << inventory.devices.size()
              << " property_errors=" << inventory.property_errors.size()
              << " output=" << output_path << '\n';
    return 0;
}

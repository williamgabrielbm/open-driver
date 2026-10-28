// Language: C++20; file: external_package_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "device_inventory.hpp"
#include "external_package_source.hpp"
#include "local_driver_store.hpp"

#include <algorithm>
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
    require(!inventory.fatal_error.has_value(), "device inventory must succeed");

    for (const auto& device : inventory.devices) {
        if (device.hardware_ids.empty()) {
            continue;
        }

        const auto local = open_driver::discover_local_driver_candidates(device);
        if (local.fatal_error || local.candidates.empty()) {
            continue;
        }

        const auto& reference = local.candidates.front();
        const auto external = open_driver::analyze_external_inf_for_device(
            device, reference.inf_path);
        if (external.fatal_error || external.candidates.empty()) {
            continue;
        }

        const auto same_node = std::find_if(
            external.candidates.begin(), external.candidates.end(),
            [&](const auto& candidate) {
                return candidate.section_name == reference.section_name &&
                       candidate.windows_rank == reference.windows_rank &&
                       candidate.driver_version.parts == reference.driver_version.parts;
            });
        require(same_node != external.candidates.end(),
                "single-INF analysis must reproduce the local driver node");
        require(same_node->package_hash == reference.package_hash,
                "single-INF analysis must preserve the INF SHA-256");

        std::cout << "external_package_tests: device=" << device.instance_id
                  << " inf=" << reference.inf_path
                  << " candidates=" << external.candidates.size()
                  << " rank=" << same_node->windows_rank << '\n';
        return 0;
    }

    require(false, "a local INF must be analyzable through DI_ENUMSINGLEINF");
    return 1;
}

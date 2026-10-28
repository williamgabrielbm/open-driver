// Language: C++20; file: local_candidate_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "local_driver_store.hpp"

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
    open_driver::DeviceRecord intel_graphics{
        .instance_id = "PCI\\VEN_8086&DEV_9A49&SUBSYS_02F30301\\3&11583659&0&10",
        .hardware_ids = {
            "PCI\\VEN_8086&DEV_9A49&SUBSYS_02F30301&REV_01",
            "PCI\\VEN_8086&DEV_9A49&SUBSYS_02F30301",
        },
        .compatible_ids = {"PCI\\VEN_8086&DEV_9A49&REV_01", "PCI\\VEN_8086&DEV_9A49"},
        .present = true,
    };
    open_driver::Candidate generic_intel_candidate;
    open_driver::select_candidate_matching_id(
        intel_graphics, "PCI\\VEN_8086&DEV_9A49", {}, generic_intel_candidate);
    require(generic_intel_candidate.match_kind == open_driver::MatchKind::hardware_id,
            "vendor/device INF identity must bind to the installed physical hardware ID");
    require(generic_intel_candidate.matching_id == intel_graphics.hardware_ids.front(),
            "generic INF identity must preserve the full installed hardware ID");

    const auto inventory = open_driver::collect_device_inventory({.present_only = true});
    require(!inventory.fatal_error.has_value(), "device inventory must succeed");

    for (const auto& device : inventory.devices) {
        if (device.hardware_ids.empty()) {
            continue;
        }

        auto discovery = open_driver::discover_local_driver_candidates(device);
        if (discovery.fatal_error || discovery.candidates.empty()) {
            continue;
        }

        const std::size_t discovered = discovery.candidates.size();
        const auto ranking = open_driver::rank_candidates(std::move(discovery.candidates));
        require(ranking.accepted.size() + ranking.rejected.size() == discovered,
                "ranking must account for every discovered candidate");

        const auto verify_candidate = [](const open_driver::Candidate& candidate) {
            return !candidate.inf_path.empty() && !candidate.package_id.empty() &&
                   !candidate.provider.empty();
        };
        for (const auto& candidate : ranking.accepted) {
            require(verify_candidate(candidate.candidate),
                    "accepted candidates must retain INF identity and provider");
        }
        for (const auto& candidate : ranking.rejected) {
            require(verify_candidate(candidate.candidate),
                    "rejected candidates must retain INF identity and provider");
        }

        std::cout << "local_candidate_tests: device=" << device.instance_id
                  << " discovered=" << discovered
                  << " accepted=" << ranking.accepted.size()
                  << " rejected=" << ranking.rejected.size()
                  << " source_errors=" << discovery.errors.size() << '\n';
        return 0;
    }

    require(false, "at least one present device must have a local compatible driver");
    return 1;
}

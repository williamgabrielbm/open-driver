// Language: C++20; file: local_driver_store.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "candidate_ranker.hpp"
#include "device_inventory.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace open_driver {

struct CandidateSourceError {
    std::string operation;
    std::uint32_t windows_error{};
    std::string message;
};

struct LocalCandidateResult {
    std::string device_instance_id;
    std::vector<Candidate> candidates;
    std::vector<CandidateSourceError> errors;
    std::optional<CandidateSourceError> fatal_error;
};

void select_candidate_matching_id(
    const DeviceRecord& device,
    const std::string& inf_hardware_id,
    const std::vector<std::string>& inf_compatible_ids,
    Candidate& candidate);

[[nodiscard]] LocalCandidateResult discover_local_driver_candidates(
    const DeviceRecord& device);

}  // namespace open_driver

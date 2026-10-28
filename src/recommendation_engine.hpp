// Language: C++20; file: recommendation_engine.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "candidate_ranker.hpp"
#include "device_inventory.hpp"

#include <optional>
#include <string>
#include <vector>

namespace open_driver {

enum class RecommendationDecision {
    update,
    keep,
    ignore,
    manual_review,
};

struct RecommendationContext {
    bool catalog_resolution_unambiguous{true};
    bool package_hash_verified{true};
    bool catalog_signature_valid{true};
    bool user_ignored{};
    bool allow_downgrade{};
};

struct RecommendationResult {
    RecommendationDecision decision{RecommendationDecision::manual_review};
    std::vector<std::string> decision_trace;
};

[[nodiscard]] RecommendationResult recommend_driver_action(
    const DeviceRecord& device,
    const Candidate& candidate,
    const RecommendationContext& context = {});

[[nodiscard]] std::optional<DriverVersion> parse_driver_version(
    const std::string& value);

[[nodiscard]] bool driver_release_is_newer(
    const std::string& installed_version,
    std::uint32_t installed_date_yyyymmdd,
    const std::string& available_version,
    std::uint32_t available_date_yyyymmdd);

[[nodiscard]] std::string recommendation_decision_name(
    RecommendationDecision decision);

}  // namespace open_driver

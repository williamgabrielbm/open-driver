// Language: C++20; file: update_plan.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "recommendation_engine.hpp"

#include <string>
#include <optional>
#include <vector>

namespace open_driver {

struct PackageEvidence {
    std::string catalog_update_id;
    std::string cab_path;
    std::string cab_sha256_base64;
    std::string catalog_path;
    std::string inf_path;
    std::string inf_sha256_hex;
    bool cab_hash_verified{};
    bool catalog_signature_valid{};
};

struct UpdatePlanOptions {
    std::string rollback_directory;
    bool create_restore_point{true};
};

struct PlannedCommand {
    std::string id;
    std::string phase;
    std::string executable;
    std::vector<std::string> arguments;
    bool mutates_system{};
    bool requires_elevation{};
};

struct UpdatePlan {
    std::uint32_t schema_version{1};
    std::string plan_id;
    bool ready{};
    std::vector<std::string> blockers;
    std::vector<std::string> preconditions;
    std::string device_instance_id;
    std::string selected_hardware_id;
    InstalledDriver installed_driver;
    Candidate candidate;
    PackageEvidence package;
    std::vector<PlannedCommand> steps;
    std::vector<PlannedCommand> rollback_steps;
};

[[nodiscard]] UpdatePlan build_update_plan(
    const DeviceRecord& device,
    const Candidate& candidate,
    const RecommendationResult& recommendation,
    const PackageEvidence& package,
    const UpdatePlanOptions& options);

[[nodiscard]] std::string update_plan_json(const UpdatePlan& plan);

struct UpdatePlanParseResult {
    std::optional<UpdatePlan> plan;
    std::string error;
};

[[nodiscard]] UpdatePlanParseResult parse_update_plan_json(const std::string& json);

[[nodiscard]] std::string planned_command_preview(const PlannedCommand& command);

}  // namespace open_driver

// Language: C++20; file: transaction_executor.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "recommendation_engine.hpp"
#include "update_plan.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace open_driver {

struct PlanObservation {
    bool device_found{};
    bool device_present{};
    std::string device_instance_id;
    InstalledDriver installed_driver;
    std::string cab_sha256_base64;
    std::string inf_sha256_hex;
    bool catalog_signature_valid{};
    std::string matching_hardware_id;
    std::uint32_t windows_rank{};
    RecommendationDecision recommendation{RecommendationDecision::manual_review};
    std::string observation_error;
};

struct ExecutionEvent {
    std::uint32_t sequence{};
    std::string phase;
    std::string step_id;
    std::string status;
    std::string message;
    std::string command_preview;
    bool mutates_system{};
};

struct ExecutionJournal {
    std::uint32_t schema_version{1};
    std::string plan_id;
    std::string mode{"dry_run"};
    bool completed{};
    bool blocked{};
    bool rollback_attempted{};
    bool rollback_completed{};
    bool reboot_required{};
    std::vector<ExecutionEvent> events;
};

struct ProcessResult {
    bool started{};
    bool timed_out{};
    std::uint32_t exit_code{};
    bool reboot_required{};
    std::string output;
    std::string error;
};

using ProcessRunner = std::function<ProcessResult(const PlannedCommand&)>;
using ObservationProvider = std::function<PlanObservation()>;
using ExecutionEventSink = std::function<bool(
    const ExecutionJournal&, const ExecutionEvent&)>;

[[nodiscard]] std::optional<std::filesystem::path> find_exported_driver_inf(
    const std::filesystem::path& root,
    const std::filesystem::path& installed_inf_name);

[[nodiscard]] ExecutionJournal execute_update_plan_dry_run(
    const UpdatePlan& plan,
    const PlanObservation& observation);

[[nodiscard]] ExecutionJournal execute_update_plan_apply(
    const UpdatePlan& plan,
    const PlanObservation& preflight_observation,
    const ProcessRunner& process_runner,
    const ObservationProvider& observe_after_change,
    const ExecutionEventSink& event_sink);

[[nodiscard]] std::string execution_event_json_line(
    const ExecutionJournal& journal,
    const ExecutionEvent& event);

}  // namespace open_driver

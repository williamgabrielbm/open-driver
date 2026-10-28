// Language: C++20; file: transaction_executor.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "transaction_executor.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

namespace open_driver {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string filename_lower(const std::string& value) {
    return lower_ascii(std::filesystem::path(value).filename().string());
}

std::string generic_bus_hardware_id(std::string value) {
    value = lower_ascii(std::move(value));
    const bool pci = value.starts_with("pci\\ven_") &&
        value.find("&dev_") != std::string::npos;
    const bool usb = value.starts_with("usb\\vid_") &&
        value.find("&pid_") != std::string::npos;
    if (!pci && !usb) return {};

    std::size_t cut = value.size();
    for (const auto* marker : {"&subsys_", "&rev_", "&cc_", "&mi_"}) {
        const auto position = value.find(marker);
        if (position != std::string::npos) cut = std::min(cut, position);
    }
    value.resize(cut);
    return value;
}

bool equivalent_hardware_id(const std::string& left, const std::string& right) {
    if (lower_ascii(left) == lower_ascii(right)) return true;
    const auto generic_left = generic_bus_hardware_id(left);
    const auto generic_right = generic_bus_hardware_id(right);
    return !generic_left.empty() && generic_left == generic_right;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec;
                } else {
                    output << character;
                }
        }
    }
    return output.str();
}

ExecutionEvent& add_event(
    ExecutionJournal& journal,
    std::string phase,
    std::string step_id,
    std::string status,
    std::string message,
    std::string preview = {},
    bool mutates = false) {
    journal.events.push_back({
        .sequence = static_cast<std::uint32_t>(journal.events.size() + 1),
        .phase = std::move(phase),
        .step_id = std::move(step_id),
        .status = std::move(status),
        .message = std::move(message),
        .command_preview = std::move(preview),
        .mutates_system = mutates,
    });
    return journal.events.back();
}

bool fail(ExecutionJournal& journal, const std::string& step, const std::string& message) {
    add_event(journal, "preflight", step, "blocked", message);
    journal.blocked = true;
    return false;
}

bool validate_command_shape(const UpdatePlan& plan, ExecutionJournal& journal) {
    std::set<std::string> ids;
    for (const auto& command : plan.steps) {
        if (!ids.insert(command.id).second) {
            return fail(journal, "validate-command-set", "duplicate command ID in plan");
        }
        const auto executable = lower_ascii(std::filesystem::path(command.executable)
                                                .filename().string());
        if (executable != "pnputil.exe" && executable != "powershell.exe") {
            return fail(journal, "validate-command-set", "plan contains an executable outside the allowlist");
        }
        if (command.id != "create-restore-point" && executable != "pnputil.exe") {
            return fail(journal, "validate-command-set", "non-restore step must use pnputil.exe");
        }
    }
    if (plan.steps.size() != 4 && plan.steps.size() != 5) {
        return fail(journal, "validate-command-set", "unexpected number of planned commands");
    }
    const bool has_restore_point = plan.steps.size() == 5;
    const std::vector<std::string> expected_order = has_restore_point
        ? std::vector<std::string>{
              "inspect-current-driver", "export-current-driver", "create-restore-point",
              "install-selected-package", "verify-selected-driver"}
        : std::vector<std::string>{
              "inspect-current-driver", "export-current-driver",
              "install-selected-package", "verify-selected-driver"};
    for (std::size_t index = 0; index < expected_order.size(); ++index) {
        if (plan.steps[index].id != expected_order[index]) {
            return fail(journal, "validate-command-set", "planned command order changed");
        }
    }
    const auto installed_inf = std::filesystem::path(plan.installed_driver.inf_path)
                                   .filename().string();
    const std::vector<std::string> inspect_arguments{
        "/enum-devices", "/instanceid", plan.device_instance_id, "/drivers"};
    const auto& inspect = plan.steps[0];
    const auto& export_driver = plan.steps[1];
    const auto& install = plan.steps[has_restore_point ? 3 : 2];
    const auto& verify = plan.steps[has_restore_point ? 4 : 3];
    if (inspect.executable != "pnputil.exe" || inspect.arguments != inspect_arguments ||
        inspect.mutates_system || inspect.requires_elevation ||
        export_driver.executable != "pnputil.exe" ||
        export_driver.arguments.size() != 3 ||
        export_driver.arguments[0] != "/export-driver" ||
        export_driver.arguments[1] != installed_inf ||
        !export_driver.mutates_system || !export_driver.requires_elevation ||
        install.executable != "pnputil.exe" ||
        install.arguments != std::vector<std::string>{
            "/add-driver", plan.candidate.inf_path, "/install"} ||
        !install.mutates_system || !install.requires_elevation ||
        verify.executable != "pnputil.exe" || verify.arguments != inspect_arguments ||
        verify.mutates_system || verify.requires_elevation) {
        return fail(journal, "validate-command-set", "planned pnputil arguments or flags changed");
    }
    if (has_restore_point) {
        const auto& restore_point = plan.steps[2];
        const std::vector<std::string> expected_arguments{
            "-NoProfile", "-Command",
            "Checkpoint-Computer -Description '" + plan.plan_id +
                "' -RestorePointType 'MODIFY_SETTINGS'"};
        if (restore_point.executable != "powershell.exe" ||
            restore_point.arguments != expected_arguments ||
            !restore_point.mutates_system || !restore_point.requires_elevation) {
            return fail(journal, "validate-command-set", "restore point command shape changed");
        }
    }
    if (plan.rollback_steps.size() != 2) {
        return fail(journal, "validate-command-set", "rollback command set changed");
    }
    const auto rollback_inf = (std::filesystem::path(export_driver.arguments[2]) /
                               installed_inf).string();
    const auto& restore = plan.rollback_steps[0];
    const auto& verify_restore = plan.rollback_steps[1];
    if (restore.id != "restore-exported-driver" ||
        restore.executable != "pnputil.exe" ||
        restore.arguments != std::vector<std::string>{
            "/add-driver", rollback_inf, "/install"} ||
        !restore.mutates_system || !restore.requires_elevation ||
        verify_restore.id != "verify-restored-driver" ||
        verify_restore.executable != "pnputil.exe" ||
        verify_restore.arguments != inspect_arguments ||
        verify_restore.mutates_system || verify_restore.requires_elevation) {
        return fail(journal, "validate-command-set", "rollback command shape changed");
    }
    return true;
}

bool validate_preflight(
    const UpdatePlan& plan,
    const PlanObservation& observation,
    ExecutionJournal& journal) {
    if (!plan.ready || !plan.blockers.empty()) {
        return fail(journal, "require-ready-plan", "plan is blocked and cannot enter preflight");
    }
    if (!observation.observation_error.empty()) {
        return fail(journal, "observe-current-state", observation.observation_error);
    }
    if (!observation.device_found || !observation.device_present) {
        return fail(journal, "require-present-device", "device is missing or no longer present");
    }
    if (lower_ascii(observation.device_instance_id) != lower_ascii(plan.device_instance_id)) {
        return fail(journal, "match-device-instance", "observed device instance differs from the plan");
    }
    if (lower_ascii(observation.installed_driver.provider) !=
            lower_ascii(plan.installed_driver.provider) ||
        observation.installed_driver.version != plan.installed_driver.version ||
        observation.installed_driver.date_yyyymmdd != plan.installed_driver.date_yyyymmdd ||
        filename_lower(observation.installed_driver.inf_path) !=
            filename_lower(plan.installed_driver.inf_path)) {
        return fail(journal, "match-installed-snapshot", "installed driver changed after plan creation");
    }
    if (observation.cab_sha256_base64 != plan.package.cab_sha256_base64) {
        return fail(journal, "verify-cab-hash", "CAB SHA-256 differs from the plan");
    }
    if (lower_ascii(observation.inf_sha256_hex) !=
        lower_ascii(plan.candidate.package_hash)) {
        return fail(journal, "verify-inf-hash", "INF SHA-256 differs from the plan");
    }
    if (!observation.catalog_signature_valid) {
        return fail(journal, "verify-catalog-signature", "catalog signature is no longer valid");
    }
    if (!equivalent_hardware_id(
            observation.matching_hardware_id, plan.selected_hardware_id)) {
        return fail(journal, "match-hardware-id", "best candidate Hardware ID changed");
    }
    if (observation.windows_rank != plan.candidate.windows_rank) {
        return fail(journal, "match-windows-rank", "Windows candidate rank changed");
    }
    if (observation.recommendation != RecommendationDecision::update) {
        return fail(journal, "recompute-recommendation", "current recommendation is not update");
    }
    if (!validate_command_shape(plan, journal)) return false;
    add_event(journal, "preflight", "validate-current-state", "passed",
              "all immutable identities and current-state observations match the plan");
    return true;
}

std::string candidate_version(const Candidate& candidate) {
    std::ostringstream output;
    output << candidate.driver_version.parts[0] << '.'
           << candidate.driver_version.parts[1] << '.'
           << candidate.driver_version.parts[2] << '.'
           << candidate.driver_version.parts[3];
    return output.str();
}

bool candidate_is_installed(const UpdatePlan& plan, const PlanObservation& observation) {
    return observation.observation_error.empty() && observation.device_found &&
           observation.device_present &&
           lower_ascii(observation.installed_driver.provider) ==
               lower_ascii(plan.candidate.provider) &&
           observation.installed_driver.version == candidate_version(plan.candidate) &&
           observation.installed_driver.date_yyyymmdd == plan.candidate.driver_date;
}

bool original_driver_is_installed(
    const UpdatePlan& plan, const PlanObservation& observation) {
    return observation.observation_error.empty() && observation.device_found &&
           observation.device_present &&
           lower_ascii(observation.installed_driver.provider) ==
               lower_ascii(plan.installed_driver.provider) &&
           observation.installed_driver.version == plan.installed_driver.version &&
           observation.installed_driver.date_yyyymmdd ==
               plan.installed_driver.date_yyyymmdd;
}

std::string bounded_output(std::string value) {
    constexpr std::size_t limit = 4096;
    if (value.size() > limit) value = value.substr(0, limit) + "...[truncated]";
    return value;
}

}  // namespace

std::optional<std::filesystem::path> find_exported_driver_inf(
    const std::filesystem::path& root,
    const std::filesystem::path& installed_inf_name) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return std::nullopt;

    std::optional<std::filesystem::path> preferred_match;
    std::optional<std::filesystem::path> only_inf;
    std::size_t inf_count = 0;
    for (std::filesystem::recursive_directory_iterator iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end && !error; iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error) continue;
        if (lower_ascii(iterator->path().extension().string()) != ".inf") continue;
        ++inf_count;
        only_inf = iterator->path();
        if (filename_lower(iterator->path().filename().string()) ==
            filename_lower(installed_inf_name.filename().string())) {
            if (preferred_match) return std::nullopt;
            preferred_match = iterator->path();
        }
    }
    if (error) return std::nullopt;
    if (preferred_match) return preferred_match;

    // PnPUtil exports the package with its original INF name. Windows stores the
    // installed copy as oemNN.inf, so a single exported INF is the same package.
    return inf_count == 1 ? only_inf : std::nullopt;
}

ExecutionJournal execute_update_plan_dry_run(
    const UpdatePlan& plan,
    const PlanObservation& observation) {
    ExecutionJournal journal{.plan_id = plan.plan_id};
    add_event(journal, "control", "load-plan", "observed",
              "plan parsed with schema version " + std::to_string(plan.schema_version));

    if (!validate_preflight(plan, observation, journal)) return journal;
    for (const auto& command : plan.steps) {
        add_event(journal, command.phase, command.id,
                  command.mutates_system ? "would_execute" : "would_observe",
                  "dry-run recorded the step without launching a process",
                  planned_command_preview(command), command.mutates_system);
    }
    add_event(journal, "control", "dry-run-complete", "completed",
              "all steps were simulated; no command was executed");
    journal.completed = true;
    return journal;
}

ExecutionJournal execute_update_plan_apply(
    const UpdatePlan& plan,
    const PlanObservation& preflight_observation,
    const ProcessRunner& process_runner,
    const ObservationProvider& observe_after_change,
    const ExecutionEventSink& event_sink) {
    ExecutionJournal journal{.plan_id = plan.plan_id, .mode = "apply"};
    add_event(journal, "control", "load-plan", "observed",
              "plan parsed with schema version " + std::to_string(plan.schema_version));
    validate_preflight(plan, preflight_observation, journal);

    std::size_t persisted = 0;
    const auto persist_new_events = [&]() {
        while (persisted < journal.events.size()) {
            if (!event_sink || !event_sink(journal, journal.events[persisted])) {
                journal.blocked = true;
                return false;
            }
            ++persisted;
        }
        return true;
    };
    if (!persist_new_events() || journal.blocked) return journal;
    if (!process_runner || !observe_after_change) {
        fail(journal, "executor-configuration", "process runner or observer is missing");
        persist_new_events();
        return journal;
    }

    bool installation_started = false;
    bool apply_failed = false;
    for (const auto& command : plan.steps) {
        add_event(journal, command.phase, command.id, "starting",
                  "command is about to run", planned_command_preview(command),
                  command.mutates_system);
        if (!persist_new_events()) return journal;
        if (command.id == "install-selected-package") installation_started = true;
        const auto result = process_runner(command);
        const bool succeeded = result.started && !result.timed_out &&
                               (result.exit_code == 0 || result.reboot_required);
        journal.reboot_required = journal.reboot_required || result.reboot_required;
        std::string message = succeeded
            ? "command completed with exit code " + std::to_string(result.exit_code) +
                  (result.reboot_required ? "; Windows restart required" : "")
            : (!result.error.empty() ? result.error
               : result.timed_out ? "command timed out"
               : "command failed with exit code " + std::to_string(result.exit_code));
        const auto output = bounded_output(result.output);
        if (!output.empty()) message += "; output: " + output;
        add_event(journal, command.phase, command.id,
                  succeeded ? "succeeded" : "failed", std::move(message),
                  planned_command_preview(command), command.mutates_system);
        if (!persist_new_events()) return journal;
        if (!succeeded) {
            apply_failed = true;
            break;
        }
    }

    if (!apply_failed) {
        const auto after = observe_after_change();
        if (candidate_is_installed(plan, after)) {
            add_event(journal, "verify", "verify-installed-state", "passed",
                      "the selected provider, version, date, and device presence were observed");
            add_event(journal, "control", "apply-complete", "completed",
                      "driver installation completed and was verified");
            journal.completed = true;
            persist_new_events();
            return journal;
        }
        add_event(journal, "verify", "verify-installed-state", "failed",
                  after.observation_error.empty()
                      ? "installed driver does not match the selected candidate"
                      : after.observation_error);
        persist_new_events();
        apply_failed = true;
    }

    if (!installation_started) {
        journal.blocked = true;
        add_event(journal, "control", "apply-stopped", "blocked",
                  "execution stopped before driver installation; rollback was not needed");
        persist_new_events();
        return journal;
    }

    journal.rollback_attempted = true;
    add_event(journal, "rollback", "rollback-start", "starting",
              "installation did not verify; rollback commands will run");
    if (!persist_new_events()) return journal;
    bool rollback_commands_succeeded = true;
    for (const auto& command : plan.rollback_steps) {
        add_event(journal, command.phase, command.id, "starting",
                  "rollback command is about to run", planned_command_preview(command),
                  command.mutates_system);
        if (!persist_new_events()) return journal;
        const auto result = process_runner(command);
        const bool succeeded = result.started && !result.timed_out &&
                               (result.exit_code == 0 || result.reboot_required);
        journal.reboot_required = journal.reboot_required || result.reboot_required;
        rollback_commands_succeeded = rollback_commands_succeeded && succeeded;
        std::string message = succeeded
            ? "rollback command completed with exit code " + std::to_string(result.exit_code) +
                  (result.reboot_required ? "; Windows restart required" : "")
            : (!result.error.empty() ? result.error
               : result.timed_out ? "rollback command timed out"
               : "rollback command failed with exit code " +
                    std::to_string(result.exit_code));
        const auto output = bounded_output(result.output);
        if (!output.empty()) message += "; output: " + output;
        add_event(journal, command.phase, command.id,
                  succeeded ? "succeeded" : "failed", std::move(message),
                  planned_command_preview(command), command.mutates_system);
        if (!persist_new_events()) return journal;
    }
    const auto after_rollback = observe_after_change();
    journal.rollback_completed = rollback_commands_succeeded &&
                                 original_driver_is_installed(plan, after_rollback);
    add_event(journal, "rollback-verify", "verify-rollback-state",
              journal.rollback_completed ? "passed" : "failed",
              journal.rollback_completed
                  ? "the original provider, version, date, and device presence were restored"
                  : "rollback did not restore the original observed driver state");
    journal.blocked = true;
    persist_new_events();
    return journal;
}

std::string execution_event_json_line(
    const ExecutionJournal& journal,
    const ExecutionEvent& event) {
    std::ostringstream output;
    output << "{\"schema_version\":" << journal.schema_version
           << ",\"plan_id\":\"" << json_escape(journal.plan_id)
           << "\",\"mode\":\"" << json_escape(journal.mode)
           << "\",\"sequence\":" << event.sequence
           << ",\"phase\":\"" << json_escape(event.phase)
           << "\",\"step_id\":\"" << json_escape(event.step_id)
           << "\",\"status\":\"" << json_escape(event.status)
           << "\",\"message\":\"" << json_escape(event.message)
           << "\",\"command_preview\":\"" << json_escape(event.command_preview)
           << "\",\"mutates_system\":" << (event.mutates_system ? "true" : "false")
           << ",\"reboot_required\":" << (journal.reboot_required ? "true" : "false")
           << "}\n";
    return output.str();
}

}  // namespace open_driver

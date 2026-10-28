// Language: C++20; file: transaction_executor_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "transaction_executor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

open_driver::UpdatePlan ready_plan() {
    open_driver::DeviceRecord device{
        .instance_id = "DISPLAY\\HWP3138\\INSTANCE",
        .hardware_ids = {"MONITOR\\HWP3138"},
        .compatible_ids = {"*PNP09FF"},
        .installed_driver = {
            .provider = "Microsoft",
            .version = "10.0.19041.5794",
            .inf_path = "C:\\Windows\\INF\\monitor.inf",
            .matching_device_id = "*PNP09FF",
            .date_yyyymmdd = 20060621,
        },
        .present = true,
    };
    open_driver::Candidate candidate{
        .package_hash = "58c2e48d6f43f00abc1234567890def058c2e48d6f43f00abc1234567890def0",
        .matching_id = "MONITOR\\HWP3138",
        .match_kind = open_driver::MatchKind::hardware_id,
        .windows_rank = 16711680,
        .driver_date = 20131118,
        .driver_version = {{1, 0, 0, 1}},
        .architecture_applicable = true,
        .os_applicable = true,
        .signature_valid = true,
        .provider = "HP",
        .inf_path = "C:\\packages\\hp\\monitor.inf",
    };
    const open_driver::PackageEvidence evidence{
        .catalog_update_id = "d29470d5-8699-4bd3-9518-651acbb78474",
        .cab_path = "C:\\packages\\hp\\package.cab",
        .cab_sha256_base64 = "3SRLGGm/MWr52Hs9tYFZ8oycVmlzWGWCW5OP0Qyzz2I=",
        .catalog_path = "C:\\packages\\hp\\monitor.cat",
        .inf_path = candidate.inf_path,
        .inf_sha256_hex = candidate.package_hash,
        .cab_hash_verified = true,
        .catalog_signature_valid = true,
    };
    return open_driver::build_update_plan(
        device, candidate,
        {.decision = open_driver::RecommendationDecision::update}, evidence,
        {.rollback_directory = "C:\\rollback\\hp-v206hz"});
}

open_driver::PlanObservation observation(const open_driver::UpdatePlan& plan) {
    return {
        .device_found = true,
        .device_present = true,
        .device_instance_id = plan.device_instance_id,
        .installed_driver = plan.installed_driver,
        .cab_sha256_base64 = plan.package.cab_sha256_base64,
        .inf_sha256_hex = plan.candidate.package_hash,
        .catalog_signature_valid = true,
        .matching_hardware_id = plan.selected_hardware_id,
        .windows_rank = plan.candidate.windows_rank,
        .recommendation = open_driver::RecommendationDecision::update,
    };
}

}  // namespace

int main() {
    const auto test_root = std::filesystem::temp_directory_path() /
        ("open-driver-exported-inf-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(test_root);
    const auto original_inf = test_root / "tigerlakepch-lpsystem.inf";
    std::ofstream(original_inf) << "[Version]\n";
    const auto original_name_match = open_driver::find_exported_driver_inf(
        test_root, "oem36.inf");
    require(original_name_match && *original_name_match == original_inf,
            "one original-name export must resolve an installed oemNN.inf name");

    const auto second_inf = test_root / "second.inf";
    std::ofstream(second_inf) << "[Version]\n";
    require(!open_driver::find_exported_driver_inf(test_root, "oem36.inf"),
            "multiple exported INFs without an exact name must be rejected");
    const auto preferred_inf = test_root / "oem36.inf";
    std::ofstream(preferred_inf) << "[Version]\n";
    const auto exact_match = open_driver::find_exported_driver_inf(test_root, "OEM36.INF");
    require(exact_match && *exact_match == preferred_inf,
            "an exact installed INF name must win when a package contains multiple INFs");
    std::filesystem::remove(preferred_inf);
    std::filesystem::remove(second_inf);
    std::filesystem::remove(original_inf);
    std::filesystem::remove(test_root);

    const auto plan = ready_plan();
    auto state = observation(plan);
    const auto success = open_driver::execute_update_plan_dry_run(plan, state);
    require(success.completed && !success.blocked, "matching state must complete dry-run");
    require(success.events.size() == plan.steps.size() + 3,
            "journal must contain load, validation, simulated steps and completion");
    require(success.events[2].status == "would_observe",
            "first planned command must be observed without execution");

    auto bus_id_plan = plan;
    bus_id_plan.selected_hardware_id =
        "PCI\\VEN_8086&DEV_9A49&SUBSYS_02F30301&REV_01";
    bus_id_plan.candidate.matching_id = bus_id_plan.selected_hardware_id;
    auto generic_bus_observation = observation(bus_id_plan);
    generic_bus_observation.matching_hardware_id = "PCI\\VEN_8086&DEV_9A49";
    const auto equivalent_bus_id = open_driver::execute_update_plan_dry_run(
        bus_id_plan, generic_bus_observation);
    require(equivalent_bus_id.completed && !equivalent_bus_id.blocked,
            "generic PCI vendor/device identity must match the same subsystem-qualified device");

    state.inf_sha256_hex[0] = state.inf_sha256_hex[0] == '0' ? '1' : '0';
    const auto changed = open_driver::execute_update_plan_dry_run(plan, state);
    require(changed.blocked && !changed.completed, "changed INF hash must stop dry-run");
    require(changed.events.back().step_id == "verify-inf-hash",
            "journal must name the first failed precondition");

    auto tampered = plan;
    tampered.steps[0].executable = "cmd.exe";
    const auto rejected = open_driver::execute_update_plan_dry_run(tampered, observation(plan));
    require(rejected.blocked, "executable outside allowlist must be rejected");

    auto blocked_plan = plan;
    blocked_plan.ready = false;
    blocked_plan.blockers = {"device absent"};
    const auto blocked = open_driver::execute_update_plan_dry_run(
        blocked_plan, observation(blocked_plan));
    require(blocked.events.size() == 2 && blocked.blocked,
            "blocked plan must stop before observing the system");

    const auto line = open_driver::execution_event_json_line(success, success.events.front());
    require(line.find("\"mode\":\"dry_run\"") != std::string::npos && line.back() == '\n',
            "journal event must serialize as one JSON line");

    std::vector<std::string> commands;
    const auto runner = [&](const open_driver::PlannedCommand& command) {
        commands.push_back(command.id);
        return open_driver::ProcessResult{.started = true, .exit_code = 0};
    };
    auto installed = observation(plan);
    installed.installed_driver.provider = plan.candidate.provider;
    installed.installed_driver.version = "1.0.0.1";
    installed.installed_driver.date_yyyymmdd = plan.candidate.driver_date;
    std::vector<std::string> persisted;
    const auto applied = open_driver::execute_update_plan_apply(
        plan, observation(plan), runner, [&] { return installed; },
        [&](const auto&, const auto& event) {
            persisted.push_back(event.step_id + ":" + event.status);
            return true;
        });
    require(applied.completed && !applied.blocked,
            "matching post-install observation must complete apply");
    require(!commands.empty() && persisted.size() == applied.events.size(),
            "every apply event must be durably offered to the sink");

    commands.clear();
    const auto rolled_back = open_driver::execute_update_plan_apply(
        plan, observation(plan), runner, [&] { return observation(plan); },
        [](const auto&, const auto&) { return true; });
    require(rolled_back.blocked && rolled_back.rollback_attempted &&
                rolled_back.rollback_completed,
            "failed post-install verification must run and verify rollback");
    require(std::find(commands.begin(), commands.end(), "restore-exported-driver") !=
                commands.end(),
            "rollback must execute the exported driver restore step");

    commands.clear();
    const auto journal_failure = open_driver::execute_update_plan_apply(
        plan, observation(plan), runner, [&] { return installed; },
        [](const auto&, const auto& event) {
            return !(event.status == "starting" && event.mutates_system);
        });
    require(journal_failure.blocked,
            "journal failure before a mutation must block apply");
    require(std::find(commands.begin(), commands.end(), "export-current-driver") ==
                commands.end(),
            "a mutating command must not run when its starting event was not persisted");

    const auto reboot_runner = [](const open_driver::PlannedCommand& command) {
        return open_driver::ProcessResult{
            .started = true,
            .exit_code = command.id == "install-selected-package" ? 3010U : 0U,
            .reboot_required = command.id == "install-selected-package",
        };
    };
    const auto reboot_success = open_driver::execute_update_plan_apply(
        plan, observation(plan), reboot_runner, [&] { return installed; },
        [](const auto&, const auto&) { return true; });
    require(reboot_success.completed && !reboot_success.blocked &&
                reboot_success.reboot_required && !reboot_success.rollback_attempted,
            "exit code 3010 must complete and request restart without rollback");
    std::cout << "transaction_executor_tests: passed\n";
    return 0;
}

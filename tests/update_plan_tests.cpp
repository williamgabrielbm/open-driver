// Language: C++20; file: update_plan_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "update_plan.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

open_driver::DeviceRecord device() {
    return {
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
}

open_driver::Candidate candidate() {
    return {
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
}

open_driver::PackageEvidence evidence() {
    return {
        .catalog_update_id = "d29470d5-8699-4bd3-9518-651acbb78474",
        .cab_path = "C:\\packages\\hp\\package.cab",
        .cab_sha256_base64 = "3SRLGGm/MWr52Hs9tYFZ8oycVmlzWGWCW5OP0Qyzz2I=",
        .catalog_path = "C:\\packages\\hp\\monitor.cat",
        .inf_path = "C:\\packages\\hp\\monitor.inf",
        .inf_sha256_hex = "58c2e48d6f43f00abc1234567890def058c2e48d6f43f00abc1234567890def0",
        .cab_hash_verified = true,
        .catalog_signature_valid = true,
    };
}

}  // namespace

int main() {
    const open_driver::RecommendationResult update{
        .decision = open_driver::RecommendationDecision::update,
        .decision_trace = {"decision=update"},
    };
    const open_driver::UpdatePlanOptions options{
        .rollback_directory = "C:\\rollback\\hp-v206hz",
        .create_restore_point = true,
    };
    const auto ready = open_driver::build_update_plan(
        device(), candidate(), update, evidence(), options);
    require(ready.ready, "validated update must produce a ready plan");
    require(ready.blockers.empty(), "ready plan must have no blockers");
    require(ready.steps.size() == 5, "plan must include inspect, export, restore point, install and verify");
    require(ready.rollback_steps.size() == 2, "plan must include rollback and rollback verification");
    require(ready.plan_id == open_driver::build_update_plan(
                device(), candidate(), update, evidence(), options).plan_id,
            "same inputs must produce the same plan ID");
    const auto serialized = open_driver::update_plan_json(ready);
    require(serialized.find("\"ready\":true") != std::string::npos,
            "serialized plan must preserve readiness");
    const auto parsed = open_driver::parse_update_plan_json(serialized);
    require(parsed.plan.has_value() && parsed.plan->plan_id == ready.plan_id,
            "serialized plan must round-trip through the strict parser");
    require(parsed.plan->steps.size() == ready.steps.size(),
            "round-trip must preserve planned commands");
    require(!open_driver::parse_update_plan_json("{\"schema_version\":1}").plan.has_value(),
            "incomplete plan must be rejected");

    auto absent = device();
    absent.present = false;
    const auto ignored = open_driver::build_update_plan(
        absent, candidate(), {.decision = open_driver::RecommendationDecision::ignore},
        evidence(), options);
    require(!ignored.ready && ignored.steps.empty(), "ignored absent device must not produce commands");

    auto bad_evidence = evidence();
    bad_evidence.cab_hash_verified = false;
    const auto bad_hash = open_driver::build_update_plan(
        device(), candidate(), update, bad_evidence, options);
    require(!bad_hash.ready, "unverified CAB hash must block the plan");

    auto compatible = candidate();
    compatible.match_kind = open_driver::MatchKind::compatible_id;
    compatible.matching_id = "*PNP09FF";
    const auto weak_match = open_driver::build_update_plan(
        device(), compatible, update, evidence(), options);
    require(!weak_match.ready, "compatible-ID-only candidate must block automatic execution");

    std::cout << "update_plan_tests: passed\n";
    return 0;
}

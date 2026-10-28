// Language: C++20; file: recommendation_engine_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "recommendation_engine.hpp"

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
            .description = "Generic PnP Monitor",
            .provider = "Microsoft",
            .version = "10.0.19041.5794",
            .matching_device_id = "*PNP09FF",
            .date_yyyymmdd = 20060621,
        },
        .present = true,
    };
}

open_driver::Candidate candidate() {
    return {
        .matching_id = "MONITOR\\HWP3138",
        .match_kind = open_driver::MatchKind::hardware_id,
        .windows_rank = 16711680,
        .driver_date = 20131118,
        .driver_version = {{1, 0, 0, 1}},
        .architecture_applicable = true,
        .os_applicable = true,
        .signature_valid = true,
        .provider = "HP",
    };
}

}  // namespace

int main() {
    auto current_device = device();
    auto remote = candidate();
    require(open_driver::recommend_driver_action(current_device, remote).decision ==
                open_driver::RecommendationDecision::update,
            "present generic compatible driver must yield to exact vendor driver");

    current_device.present = false;
    require(open_driver::recommend_driver_action(current_device, remote).decision ==
                open_driver::RecommendationDecision::ignore,
            "absent device must be ignored");
    current_device.present = true;

    current_device.installed_driver.provider = "HP";
    current_device.installed_driver.version = "1.0.0.1";
    current_device.installed_driver.matching_device_id = "MONITOR\\HWP3138";
    require(open_driver::recommend_driver_action(current_device, remote).decision ==
                open_driver::RecommendationDecision::keep,
            "same provider and version must be kept");

    current_device.installed_driver.version = "2.0.0.0";
    current_device.installed_driver.date_yyyymmdd = 20200101;
    require(open_driver::recommend_driver_action(current_device, remote).decision ==
                open_driver::RecommendationDecision::keep,
            "downgrade must be suppressed");

    remote.driver_date = 20250101;
    require(open_driver::recommend_driver_action(current_device, remote).decision ==
                open_driver::RecommendationDecision::update,
            "a newer signed INF date must allow a vendor version-branch change");

    remote.signature_valid = false;
    require(open_driver::recommend_driver_action(current_device, remote).decision ==
                open_driver::RecommendationDecision::ignore,
            "invalid signature must be ignored");
    remote.signature_valid = true;

    const open_driver::RecommendationContext ambiguous{.catalog_resolution_unambiguous = false};
    require(open_driver::recommend_driver_action(current_device, remote, ambiguous).decision ==
                open_driver::RecommendationDecision::manual_review,
            "ambiguous catalog result must require manual review");

    const open_driver::RecommendationContext ignored{.user_ignored = true};
    require(open_driver::recommend_driver_action(current_device, remote, ignored).decision ==
                open_driver::RecommendationDecision::ignore,
            "ignore list must win over all other policy");

    require(open_driver::parse_driver_version("10.0.19041.5794").has_value(),
            "four-part version must parse");
    require(!open_driver::parse_driver_version("1.2.bad.4").has_value(),
            "non-numeric version must fail parsing");
    require(open_driver::driver_release_is_newer(
                "6001.15.165.3", 20260713, "6001.15.123.362", 20260717),
            "a later Realtek DriverVer date must win across version branches");
    require(!open_driver::driver_release_is_newer(
                "6001.15.165.3", 20260713, "6001.15.123.362", 20260712),
            "an older DriverVer date must not be offered as an update");
    require(open_driver::driver_release_is_newer(
                "6001.15.123.362", 20260717, "6001.15.165.4", 20260717),
            "the four-part version must break equal-date ties");
    require(!open_driver::driver_release_is_newer(
                "32.0.101.7092", 20260701, "32.0.101.7092", 20260925),
            "an identical version must never become an initial update offer");
    std::cout << "recommendation_engine_tests: passed\n";
    return 0;
}

// Language: C++20; file: update_plan_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "external_package_source.hpp"
#include "microsoft_catalog.hpp"
#include "package_materializer.hpp"
#include "recommendation_engine.hpp"
#include "update_plan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

void usage() {
    std::cerr << "usage: open_driver_update_plan --inf <path> --instance <id> "
                 "--catalog-id <guid> --cab <path> --cab-sha256 <base64> "
                 "--catalog <path> --rollback-dir <path> --output <new-path> "
                 "[--no-restore-point]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string inf_path;
    std::string instance_id;
    std::string catalog_id;
    std::string cab_path;
    std::string cab_sha256;
    std::string catalog_path;
    std::string rollback_directory;
    std::string output_path;
    bool create_restore_point = true;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto value = [&](std::string& destination) {
            if (index + 1 >= argc) return false;
            destination = argv[++index];
            return true;
        };
        if (argument == "--inf") {
            if (!value(inf_path)) { usage(); return 2; }
        } else if (argument == "--instance") {
            if (!value(instance_id)) { usage(); return 2; }
        } else if (argument == "--catalog-id") {
            if (!value(catalog_id)) { usage(); return 2; }
        } else if (argument == "--cab") {
            if (!value(cab_path)) { usage(); return 2; }
        } else if (argument == "--cab-sha256") {
            if (!value(cab_sha256)) { usage(); return 2; }
        } else if (argument == "--catalog") {
            if (!value(catalog_path)) { usage(); return 2; }
        } else if (argument == "--rollback-dir") {
            if (!value(rollback_directory)) { usage(); return 2; }
        } else if (argument == "--output") {
            if (!value(output_path)) { usage(); return 2; }
        } else if (argument == "--no-restore-point") {
            create_restore_point = false;
        } else {
            usage();
            return 2;
        }
    }
    if (inf_path.empty() || instance_id.empty() || catalog_id.empty() || cab_path.empty() ||
        cab_sha256.empty() || catalog_path.empty() || rollback_directory.empty() ||
        output_path.empty()) {
        usage();
        return 2;
    }
    if (std::filesystem::exists(output_path)) {
        std::cerr << "output path already exists: " << output_path << '\n';
        return 4;
    }

    std::error_code path_error;
    const auto absolute_inf = std::filesystem::absolute(inf_path, path_error);
    if (path_error) {
        std::cerr << "could not resolve INF path\n";
        return 1;
    }
    const auto absolute_cab = std::filesystem::absolute(cab_path, path_error);
    if (path_error) {
        std::cerr << "could not resolve CAB path\n";
        return 1;
    }
    const auto absolute_catalog = std::filesystem::absolute(catalog_path, path_error);
    if (path_error) {
        std::cerr << "could not resolve catalog path\n";
        return 1;
    }
    const auto absolute_rollback = std::filesystem::absolute(rollback_directory, path_error);
    if (path_error) {
        std::cerr << "could not resolve rollback directory\n";
        return 1;
    }

    const auto inventory = open_driver::collect_device_inventory({.present_only = false});
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->operation << ": "
                  << inventory.fatal_error->message << '\n';
        return 1;
    }
    const auto requested = upper_ascii(instance_id);
    const auto device = std::find_if(inventory.devices.begin(), inventory.devices.end(),
        [&](const auto& value) { return upper_ascii(value.instance_id) == requested; });
    if (device == inventory.devices.end()) {
        std::cerr << "device instance was not found: " << instance_id << '\n';
        return 3;
    }

    auto discovery = open_driver::analyze_external_inf_for_device(*device, absolute_inf.string());
    if (discovery.fatal_error) {
        std::cerr << discovery.fatal_error->operation << ": "
                  << discovery.fatal_error->message << '\n';
        return 1;
    }
    auto ranking = open_driver::rank_candidates(std::move(discovery.candidates));
    if (ranking.accepted.empty()) {
        std::cerr << "the INF produced no accepted candidate for the device\n";
        return 5;
    }

    const auto& candidate = ranking.accepted.front().candidate;
    std::string catalog_status;
    const bool catalog_valid = open_driver::verify_catalog_signature_file(
        absolute_catalog, catalog_status);
    const bool cab_hash_valid =
        open_driver::sha256_file_base64(absolute_cab) == cab_sha256;
    const open_driver::RecommendationContext context{
        .catalog_resolution_unambiguous = true,
        .package_hash_verified = cab_hash_valid,
        .catalog_signature_valid = catalog_valid,
    };
    const auto recommendation = open_driver::recommend_driver_action(*device, candidate, context);
    const open_driver::PackageEvidence evidence{
        .catalog_update_id = catalog_id,
        .cab_path = absolute_cab.string(),
        .cab_sha256_base64 = cab_sha256,
        .catalog_path = absolute_catalog.string(),
        .inf_path = absolute_inf.string(),
        .inf_sha256_hex = candidate.package_hash,
        .cab_hash_verified = cab_hash_valid,
        .catalog_signature_valid = catalog_valid,
    };
    const open_driver::UpdatePlanOptions options{
        .rollback_directory = absolute_rollback.string(),
        .create_restore_point = create_restore_point,
    };
    const auto plan = open_driver::build_update_plan(
        *device, candidate, recommendation, evidence, options);

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        std::cerr << "could not create output report: " << output_path << '\n';
        return 1;
    }
    output << open_driver::update_plan_json(plan);
    if (!output) {
        std::cerr << "could not write output report: " << output_path << '\n';
        return 1;
    }
    std::cout << "plan_id=" << plan.plan_id
              << " ready=" << (plan.ready ? "true" : "false")
              << " blockers=" << plan.blockers.size()
              << " catalog_status=" << catalog_status
              << " output=" << output_path << '\n';
    return 0;
}

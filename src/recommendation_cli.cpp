// Language: C++20; file: recommendation_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "external_package_source.hpp"
#include "recommendation_engine.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
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

void write_string(std::ostream& output, const std::string& value) {
    output << '"' << json_escape(value) << '"';
}

std::string version_string(const open_driver::DriverVersion& version) {
    std::ostringstream output;
    output << version.parts[0] << '.' << version.parts[1] << '.'
           << version.parts[2] << '.' << version.parts[3];
    return output.str();
}

void write_string_array(std::ostream& output, const std::vector<std::string>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        write_string(output, values[index]);
    }
    output << ']';
}

bool write_report(
    const std::string& path,
    const open_driver::DeviceRecord& device,
    const open_driver::Candidate& candidate,
    const open_driver::RecommendationContext& context,
    const open_driver::RecommendationResult& result) {
    if (std::filesystem::exists(path)) return false;
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;

    output << "{\n  \"schema_version\":1,\n  \"device\":{\"instance_id\":";
    write_string(output, device.instance_id);
    output << ",\"present\":" << (device.present ? "true" : "false")
           << ",\"description\":";
    write_string(output, device.friendly_name.empty() ? device.description : device.friendly_name);
    output << ",\"installed_driver\":{\"provider\":";
    write_string(output, device.installed_driver.provider);
    output << ",\"version\":";
    write_string(output, device.installed_driver.version);
    output << ",\"date\":" << device.installed_driver.date_yyyymmdd
           << ",\"matching_device_id\":";
    write_string(output, device.installed_driver.matching_device_id);
    output << "}},\n  \"candidate\":{\"inf_path\":";
    write_string(output, candidate.inf_path);
    output << ",\"provider\":";
    write_string(output, candidate.provider);
    output << ",\"version\":";
    write_string(output, version_string(candidate.driver_version));
    output << ",\"date\":" << candidate.driver_date
           << ",\"matching_id\":";
    write_string(output, candidate.matching_id);
    output << ",\"match_kind\":";
    write_string(output, candidate.match_kind == open_driver::MatchKind::hardware_id
        ? "hardware_id" : "compatible_id");
    output << ",\"windows_rank\":" << candidate.windows_rank
           << ",\"signature_valid\":" << (candidate.signature_valid ? "true" : "false")
           << ",\"architecture_applicable\":"
           << (candidate.architecture_applicable ? "true" : "false")
           << ",\"os_applicable\":" << (candidate.os_applicable ? "true" : "false")
           << "},\n  \"context\":{\"catalog_resolution_unambiguous\":"
           << (context.catalog_resolution_unambiguous ? "true" : "false")
           << ",\"package_hash_verified\":"
           << (context.package_hash_verified ? "true" : "false")
           << ",\"catalog_signature_valid\":"
           << (context.catalog_signature_valid ? "true" : "false")
           << ",\"user_ignored\":" << (context.user_ignored ? "true" : "false")
           << ",\"allow_downgrade\":" << (context.allow_downgrade ? "true" : "false")
           << "},\n  \"decision\":";
    write_string(output, open_driver::recommendation_decision_name(result.decision));
    output << ",\n  \"decision_trace\":";
    write_string_array(output, result.decision_trace);
    output << "\n}\n";
    return static_cast<bool>(output);
}

void usage() {
    std::cerr << "usage: open_driver_recommendation --inf <path> --instance <id> "
                 "--output <new-path> [--catalog-ambiguous] [--hash-unverified] "
                 "[--catalog-unverified] [--user-ignored] [--allow-downgrade]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string inf_path;
    std::string instance_id;
    std::string output_path;
    open_driver::RecommendationContext context;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--inf" && index + 1 < argc) {
            inf_path = argv[++index];
        } else if (argument == "--instance" && index + 1 < argc) {
            instance_id = argv[++index];
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else if (argument == "--catalog-ambiguous") {
            context.catalog_resolution_unambiguous = false;
        } else if (argument == "--hash-unverified") {
            context.package_hash_verified = false;
        } else if (argument == "--catalog-unverified") {
            context.catalog_signature_valid = false;
        } else if (argument == "--user-ignored") {
            context.user_ignored = true;
        } else if (argument == "--allow-downgrade") {
            context.allow_downgrade = true;
        } else {
            usage();
            return 2;
        }
    }
    if (inf_path.empty() || instance_id.empty() || output_path.empty()) {
        usage();
        return 2;
    }
    if (std::filesystem::exists(output_path)) {
        std::cerr << "output path already exists: " << output_path << '\n';
        return 4;
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

    auto discovery = open_driver::analyze_external_inf_for_device(*device, inf_path);
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
    const auto recommendation = open_driver::recommend_driver_action(*device, candidate, context);
    if (!write_report(output_path, *device, candidate, context, recommendation)) {
        std::cerr << "could not create output report: " << output_path << '\n';
        return 1;
    }
    std::cout << "decision=" << open_driver::recommendation_decision_name(recommendation.decision)
              << " output=" << output_path << '\n';
    return 0;
}

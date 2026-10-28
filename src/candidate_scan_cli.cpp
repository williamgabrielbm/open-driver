// Language: C++20; file: candidate_scan_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "local_driver_store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct DeviceRanking {
    std::string instance_id;
    std::string description;
    open_driver::RankingResult ranking;
    std::vector<open_driver::CandidateSourceError> source_errors;
};

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
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
        if (index != 0) {
            output << ',';
        }
        write_string(output, values[index]);
    }
    output << ']';
}

void write_candidate(
    std::ostream& output,
    const open_driver::Candidate& candidate,
    const std::vector<std::string>& reasons,
    bool accepted) {
    output << "{\"accepted\":" << (accepted ? "true" : "false")
           << ",\"package_id\":";
    write_string(output, candidate.package_id);
    output << ",\"package_hash\":";
    write_string(output, candidate.package_hash);
    output << ",\"inf_path\":";
    write_string(output, candidate.inf_path);
    output << ",\"section_name\":";
    write_string(output, candidate.section_name);
    output << ",\"provider\":";
    write_string(output, candidate.provider);
    output << ",\"description\":";
    write_string(output, candidate.description);
    output << ",\"matching_id\":";
    write_string(output, candidate.matching_id);
    output << ",\"match_kind\":";
    write_string(output, candidate.match_kind == open_driver::MatchKind::hardware_id
        ? "hardware_id" : "compatible_id");
    output << ",\"windows_rank\":" << candidate.windows_rank
           << ",\"driver_date\":" << candidate.driver_date
           << ",\"driver_version\":";
    write_string(output, version_string(candidate.driver_version));
    output << ",\"currently_installed\":"
           << (candidate.currently_installed ? "true" : "false")
           << ",\"inf_signed\":" << (candidate.inf_signed ? "true" : "false")
           << ",\"authenticode_signed\":"
           << (candidate.authenticode_signed ? "true" : "false")
           << ",\"blocked_by_windows\":"
           << (candidate.blocked_by_windows ? "true" : "false")
           << ",\"inf_hardware_id\":";
    write_string(output, candidate.inf_hardware_id);
    output << ",\"inf_compatible_ids\":";
    write_string_array(output, candidate.inf_compatible_ids);
    output << ",\"reasons\":";
    write_string_array(output, reasons);
    output << '}';
}

bool write_report(const std::string& path, const std::vector<DeviceRanking>& devices) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    output << "{\n  \"schema_version\": 1,\n  \"devices\": [";
    for (std::size_t device_index = 0; device_index < devices.size(); ++device_index) {
        const auto& device = devices[device_index];
        output << (device_index == 0 ? "\n" : ",\n") << "    {\"instance_id\":";
        write_string(output, device.instance_id);
        output << ",\"description\":";
        write_string(output, device.description);
        output << ",\"accepted\":[";
        for (std::size_t index = 0; index < device.ranking.accepted.size(); ++index) {
            if (index != 0) {
                output << ',';
            }
            write_candidate(
                output, device.ranking.accepted[index].candidate,
                device.ranking.accepted[index].decision_trace, true);
        }
        output << "],\"rejected\":[";
        for (std::size_t index = 0; index < device.ranking.rejected.size(); ++index) {
            if (index != 0) {
                output << ',';
            }
            write_candidate(
                output, device.ranking.rejected[index].candidate,
                device.ranking.rejected[index].reasons, false);
        }
        output << "]}";
    }
    output << "\n  ]\n}\n";
    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t limit = 3;
    std::string output_path = "local-candidates.json";

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--limit" && index + 1 < argc) {
            limit = static_cast<std::size_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else {
            std::cerr << "usage: open_driver_candidates [--limit <count>] [--output <path>]\n";
            return 2;
        }
    }

    const auto inventory = open_driver::collect_device_inventory({.present_only = true});
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->operation << ": "
                  << inventory.fatal_error->message << '\n';
        return 1;
    }

    std::vector<DeviceRanking> devices;
    std::size_t attempted = 0;
    for (const auto& device : inventory.devices) {
        if (devices.size() >= limit || device.hardware_ids.empty()) {
            continue;
        }
        ++attempted;
        auto discovery = open_driver::discover_local_driver_candidates(device);
        if (discovery.fatal_error || discovery.candidates.empty()) {
            continue;
        }
        devices.push_back({
            .instance_id = device.instance_id,
            .description = device.friendly_name.empty() ? device.description : device.friendly_name,
            .ranking = open_driver::rank_candidates(std::move(discovery.candidates)),
            .source_errors = std::move(discovery.errors),
        });
    }

    if (!write_report(output_path, devices)) {
        std::cerr << "could not write " << output_path << '\n';
        return 1;
    }

    std::size_t accepted = 0;
    std::size_t rejected = 0;
    for (const auto& device : devices) {
        accepted += device.ranking.accepted.size();
        rejected += device.ranking.rejected.size();
    }
    std::cout << "attempted=" << attempted << " devices=" << devices.size()
              << " accepted=" << accepted << " rejected=" << rejected
              << " output=" << output_path << '\n';
    return devices.empty() ? 1 : 0;
}

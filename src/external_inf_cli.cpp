// Language: C++20; file: external_inf_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "external_package_source.hpp"

#include <algorithm>
#include <cctype>
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

struct MatchReport {
    std::string instance_id;
    std::string description;
    open_driver::RankingResult ranking;
};

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

void write_candidate(std::ostream& output, const open_driver::Candidate& candidate) {
    output << "{\"inf_path\":";
    write_string(output, candidate.inf_path);
    output << ",\"section_name\":";
    write_string(output, candidate.section_name);
    output << ",\"provider\":";
    write_string(output, candidate.provider);
    output << ",\"description\":";
    write_string(output, candidate.description);
    output << ",\"matching_id\":";
    write_string(output, candidate.matching_id);
    output << ",\"windows_rank\":" << candidate.windows_rank
           << ",\"driver_date\":" << candidate.driver_date
           << ",\"driver_version\":";
    write_string(output, version_string(candidate.driver_version));
    output << ",\"package_hash\":";
    write_string(output, candidate.package_hash);
    output << ",\"inf_signed\":" << (candidate.inf_signed ? "true" : "false")
           << ",\"authenticode_signed\":"
           << (candidate.authenticode_signed ? "true" : "false")
           << ",\"blocked_by_windows\":"
           << (candidate.blocked_by_windows ? "true" : "false") << '}';
}

bool write_report(
    const std::string& path,
    const std::string& inf_path,
    std::size_t attempted,
    const std::vector<MatchReport>& matches) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "{\n  \"schema_version\":1,\n  \"inf_path\":";
    write_string(output, inf_path);
    output << ",\n  \"devices_attempted\":" << attempted
           << ",\n  \"matching_devices\":[";
    for (std::size_t device_index = 0; device_index < matches.size(); ++device_index) {
        const auto& match = matches[device_index];
        output << (device_index == 0 ? "\n" : ",\n") << "    {\"instance_id\":";
        write_string(output, match.instance_id);
        output << ",\"description\":";
        write_string(output, match.description);
        output << ",\"accepted\":[";
        for (std::size_t index = 0; index < match.ranking.accepted.size(); ++index) {
            if (index != 0) output << ',';
            write_candidate(output, match.ranking.accepted[index].candidate);
        }
        output << "],\"rejected\":[";
        for (std::size_t index = 0; index < match.ranking.rejected.size(); ++index) {
            if (index != 0) output << ',';
            write_candidate(output, match.ranking.rejected[index].candidate);
        }
        output << "]}";
    }
    output << "\n  ]\n}\n";
    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char** argv) {
    std::string inf_path;
    std::string requested_instance;
    std::string output_path = "external-inf-analysis.json";
    std::size_t attempt_limit = 64;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--inf" && index + 1 < argc) {
            inf_path = argv[++index];
        } else if (argument == "--instance" && index + 1 < argc) {
            requested_instance = argv[++index];
        } else if (argument == "--attempt-limit" && index + 1 < argc) {
            attempt_limit = static_cast<std::size_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else {
            std::cerr << "usage: open_driver_external_inf --inf <path> "
                         "[--instance <id>] [--attempt-limit <count>] [--output <path>]\n";
            return 2;
        }
    }
    if (inf_path.empty()) {
        std::cerr << "--inf is required\n";
        return 2;
    }

    const auto inventory = open_driver::collect_device_inventory({.present_only = true});
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->operation << ": "
                  << inventory.fatal_error->message << '\n';
        return 1;
    }

    const auto normalized_instance = upper_ascii(requested_instance);
    std::size_t attempted = 0;
    std::vector<MatchReport> matches;
    for (const auto& device : inventory.devices) {
        if (!requested_instance.empty() && upper_ascii(device.instance_id) != normalized_instance) {
            continue;
        }
        if (device.hardware_ids.empty() || attempted >= attempt_limit) {
            continue;
        }

        ++attempted;
        auto discovery = open_driver::analyze_external_inf_for_device(device, inf_path);
        if (discovery.fatal_error || discovery.candidates.empty()) {
            continue;
        }
        matches.push_back({
            .instance_id = device.instance_id,
            .description = device.friendly_name.empty() ? device.description : device.friendly_name,
            .ranking = open_driver::rank_candidates(std::move(discovery.candidates)),
        });
    }

    if (!write_report(output_path, inf_path, attempted, matches)) {
        std::cerr << "could not write " << output_path << '\n';
        return 1;
    }
    std::cout << "attempted=" << attempted << " matching_devices=" << matches.size()
              << " output=" << output_path << '\n';
    return matches.empty() ? 3 : 0;
}

// Language: C++20; file: windows_update_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "device_inventory.hpp"
#include "windows_update_source.hpp"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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
    const open_driver::WindowsUpdateSearchResult& result,
    bool online) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "{\n  \"schema_version\":1,\n  \"online\":"
           << (online ? "true" : "false")
           << ",\n  \"operation_result_code\":" << result.operation_result_code
           << ",\n  \"updates\":[";
    for (std::size_t index = 0; index < result.updates.size(); ++index) {
        const auto& update = result.updates[index];
        output << (index == 0 ? "\n" : ",\n") << "    {\"update_id\":";
        write_string(output, update.update_id);
        output << ",\"revision_number\":" << update.revision_number << ",\"title\":";
        write_string(output, update.title);
        output << ",\"description\":";
        write_string(output, update.description);
        output << ",\"driver_class\":";
        write_string(output, update.driver_class);
        output << ",\"driver_hardware_id\":";
        write_string(output, update.driver_hardware_id);
        output << ",\"manufacturer\":";
        write_string(output, update.manufacturer);
        output << ",\"model\":";
        write_string(output, update.model);
        output << ",\"provider\":";
        write_string(output, update.provider);
        output << ",\"driver_date\":" << update.driver_date_yyyymmdd
               << ",\"device_problem_number\":" << update.device_problem_number
               << ",\"device_status\":" << update.device_status
               << ",\"downloaded\":" << (update.downloaded ? "true" : "false")
               << ",\"hidden\":" << (update.hidden ? "true" : "false")
               << ",\"mandatory\":" << (update.mandatory ? "true" : "false")
               << ",\"matching_instance_ids\":";
        write_string_array(output, update.matching_instance_ids);
        output << ",\"matching_present_instance_ids\":";
        write_string_array(output, update.matching_present_instance_ids);
        output << '}';
    }
    output << "\n  ]";
    if (result.fatal_error) {
        output << ",\n  \"fatal_error\":{\"operation\":";
        write_string(output, result.fatal_error->operation);
        output << ",\"hresult\":" << result.fatal_error->hresult
               << ",\"message\":";
        write_string(output, result.fatal_error->message);
        output << '}';
    } else {
        output << ",\n  \"fatal_error\":null";
    }
    output << "\n}\n";
    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char** argv) {
    bool online = true;
    std::string output_path = "windows-update-drivers.json";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--offline") {
            online = false;
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else {
            std::cerr << "usage: open_driver_windows_update [--offline] [--output <path>]\n";
            return 2;
        }
    }

    const auto inventory = open_driver::collect_device_inventory({.present_only = false});
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->operation << ": "
                  << inventory.fatal_error->message << '\n';
        return 1;
    }

    const auto result = open_driver::search_windows_update_drivers(inventory.devices, online);
    if (!write_report(output_path, result, online)) {
        std::cerr << "could not write " << output_path << '\n';
        return 1;
    }
    if (result.fatal_error) {
        std::cerr << result.fatal_error->operation << ": "
                  << result.fatal_error->message << '\n';
        return 1;
    }

    std::size_t matched = 0;
    std::size_t matched_present = 0;
    for (const auto& update : result.updates) {
        matched += !update.matching_instance_ids.empty();
        matched_present += !update.matching_present_instance_ids.empty();
    }
    std::cout << "updates=" << result.updates.size()
              << " matched_to_inventory=" << matched
              << " matched_to_present=" << matched_present
              << " operation_result=" << result.operation_result_code
              << " output=" << output_path << '\n';
    return 0;
}

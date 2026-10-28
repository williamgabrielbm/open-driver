// Language: C++20; file: update_discovery_cli.cpp; target: Windows 10/11; runtime: MSVC/CMake.
#include "device_inventory.hpp"
#include "update_orchestrator.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            default:
                if (character < 0x20) output << "\\u" << std::hex << std::setw(4)
                    << std::setfill('0') << static_cast<unsigned int>(character) << std::dec;
                else output << character;
        }
    }
    return output.str();
}
}

int main(int argc, char** argv) {
    std::string output_path = "update-discovery.json";
    std::string prepare_root;
    std::string validate_root;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--output" && index + 1 < argc) output_path = argv[++index];
        else if (argument == "--prepare-first" && index + 1 < argc) prepare_root = argv[++index];
        else if (argument == "--validate-all" && index + 1 < argc) validate_root = argv[++index];
        else {
            std::cerr << "usage: open_driver_discover_updates [--output <path>] "
                         "[--prepare-first <cache-root>] [--validate-all <cache-root>]\n";
            return 2;
        }
    }
    const auto inventory = open_driver::collect_device_inventory({.present_only = true});
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->message << '\n';
        return 1;
    }
    auto report = open_driver::discover_updates(inventory.devices,
        [](std::size_t current, std::size_t total, const std::string& name) {
            std::cout << "[" << current << "/" << total << "] " << name << '\n';
        });
    if (!validate_root.empty()) {
        open_driver::validate_and_prepare_offers(report, validate_root,
            [](std::size_t current, std::size_t total, const std::string& name) {
                std::cout << "[validate " << current << "/" << total << "] " << name << '\n';
            });
    }
    std::ofstream output(output_path, std::ios::binary);
    output << "{\n  \"windows_build\":" << report.target.build
           << ",\n  \"complete\":" << (report.complete ? "true" : "false")
           << ",\n  \"devices_detected\":" << report.devices_detected
           << ",\n  \"devices_checked\":" << report.devices_checked
           << ",\n  \"packages_checked\":" << report.packages_checked
           << ",\n  \"source_checks_required\":" << report.source_checks_required
           << ",\n  \"source_checks_completed\":" << report.source_checks_completed
           << ",\n  \"candidates_validated\":" << report.candidates_validated
           << ",\n  \"candidates_rejected\":" << report.candidates_rejected
           << ",\n  \"offers\":[";
    for (std::size_t index = 0; index < report.offers.size(); ++index) {
        const auto& offer = report.offers[index];
        output << (index == 0 ? "\n    {" : ",\n    {")
               << "\"device\":\"" << escape(offer.device.friendly_name.empty()
                    ? offer.device.description : offer.device.friendly_name) << "\","
               << "\"installed\":\"" << escape(offer.device.installed_driver.version) << "\","
               << "\"available\":\"" << escape(offer.available_version) << "\","
               << "\"date\":" << offer.available_date_yyyymmdd << ','
               << "\"related_devices\":" << offer.related_devices << ','
               << "\"catalog_update_id\":\"" << escape(offer.search.catalog_update_id) << "\","
               << "\"products\":\"" << escape(offer.search.products) << "\"}";
    }
    output << (report.offers.empty() ? "]" : "\n  ]") << ",\n  \"errors\":[";
    for (std::size_t index = 0; index < report.errors.size(); ++index) {
        output << (index == 0 ? "\n    \"" : ",\n    \"")
               << escape(report.errors[index]) << '"';
    }
    output << (report.errors.empty() ? "]" : "\n  ]") << ",\n  \"rejections\":[";
    for (std::size_t index = 0; index < report.rejections.size(); ++index) {
        output << (index == 0 ? "\n    \"" : ",\n    \"")
               << escape(report.rejections[index]) << '"';
    }
    output << (report.rejections.empty() ? "]" : "\n  ]") << "\n}\n";
    std::cout << "offers=" << report.offers.size() << " errors=" << report.errors.size()
              << " output=" << output_path << '\n';
    if (!prepare_root.empty()) {
        if (report.offers.empty()) {
            std::cerr << "there is no offer to prepare\n";
            return 3;
        }
        const auto prepared = open_driver::prepare_update(report.offers.front(), prepare_root);
        if (!prepared.prepared) {
            std::cerr << "prepare failed: " << prepared.error << '\n';
            return 4;
        }
        std::cout << "prepared_plan=" << prepared.prepared->plan_path.string()
                  << " plan_id=" << prepared.prepared->plan_id << '\n';
    }
    return output ? 0 : 2;
}

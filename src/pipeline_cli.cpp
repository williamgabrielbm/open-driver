// Language: C++20; file: pipeline_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>

#include "catalog_resolver.hpp"
#include "device_inventory.hpp"
#include "microsoft_catalog.hpp"
#include "pipeline.hpp"
#include "windows_update_source.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

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
                } else output << character;
        }
    }
    return output.str();
}

void string_value(std::ostream& output, const std::string& value) {
    output << '"' << json_escape(value) << '"';
}

void string_array(std::ostream& output, const std::vector<std::string>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        string_value(output, values[index]);
    }
    output << ']';
}

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::string system_architecture() {
    SYSTEM_INFO information{};
    GetNativeSystemInfo(&information);
    switch (information.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "AMD64";
        case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "X86";
        default: return "UNKNOWN";
    }
}

std::string system_language() {
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> locale{};
    if (GetUserDefaultLocaleName(locale.data(), static_cast<int>(locale.size())) == 0) return {};
    std::array<wchar_t, 128> language{};
    std::array<wchar_t, 128> country{};
    if (GetLocaleInfoEx(locale.data(), LOCALE_SENGLISHLANGUAGENAME,
                        language.data(), static_cast<int>(language.size())) == 0) return {};
    if (GetLocaleInfoEx(locale.data(), LOCALE_SENGLISHCOUNTRYNAME,
                        country.data(), static_cast<int>(country.size())) == 0) {
        return utf8_from_wide(language.data());
    }
    return utf8_from_wide(language.data()) + " (" + utf8_from_wide(country.data()) + ")";
}

std::string catalog_error_text(const open_driver::CatalogError& error) {
    return error.operation + ": " + error.message;
}

void resolve_catalog(open_driver::PipelineItem& item) {
    const open_driver::CatalogResolutionTarget target{
        .title = item.update.title,
        .hardware_id = item.update.driver_hardware_id,
        .architecture = system_architecture(),
        .language = system_language(),
        .provider = item.update.provider,
        .driver_date_yyyymmdd = item.update.driver_date_yyyymmdd,
    };
    const auto search = open_driver::search_microsoft_update_catalog(target.title);
    if (search.error) {
        open_driver::apply_catalog_error(item, catalog_error_text(*search.error));
        return;
    }
    std::vector<open_driver::CatalogDetails> details;
    details.reserve(search.entries.size());
    for (const auto& entry : search.entries) {
        const auto result = open_driver::get_microsoft_update_catalog_details(
            entry.catalog_update_id);
        if (result.error || !result.details) {
            open_driver::apply_catalog_error(
                item, result.error ? catalog_error_text(*result.error)
                                   : "catalog details were empty");
            return;
        }
        details.push_back(*result.details);
    }
    open_driver::apply_catalog_resolution(
        item, open_driver::resolve_catalog_candidates(target, search.entries, details));
}

bool write_report(
    const std::filesystem::path& path,
    const open_driver::PipelineReport& report,
    bool online,
    bool catalog_enabled,
    std::int32_t wua_result) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    const auto count = [&](open_driver::PipelineStatus status) {
        return std::count_if(report.items.begin(), report.items.end(),
            [&](const auto& item) { return item.status == status; });
    };
    output << "{\n  \"schema_version\":1,\n  \"mode\":\"read_only\",\n"
           << "  \"online_windows_update\":" << (online ? "true" : "false") << ",\n"
           << "  \"catalog_lookup_enabled\":" << (catalog_enabled ? "true" : "false") << ",\n"
           << "  \"windows_update_result_code\":" << wua_result << ",\n"
           << "  \"summary\":{\"known_devices\":" << report.inventory_devices
           << ",\"present_devices\":" << report.present_devices
           << ",\"offers\":" << report.items.size()
           << ",\"present_device_offers\":"
           << count(open_driver::PipelineStatus::catalog_lookup_pending) +
              count(open_driver::PipelineStatus::catalog_lookup_failed) +
              count(open_driver::PipelineStatus::catalog_ambiguous) +
              count(open_driver::PipelineStatus::catalog_selected)
           << ",\"skipped_absent\":"
           << count(open_driver::PipelineStatus::skipped_absent_device)
           << ",\"catalog_selected\":" << count(open_driver::PipelineStatus::catalog_selected)
           << "},\n  \"items\":[";
    for (std::size_t index = 0; index < report.items.size(); ++index) {
        const auto& item = report.items[index];
        output << (index == 0 ? "\n    {" : ",\n    {") << "\"windows_update_id\":";
        string_value(output, item.update.update_id);
        output << ",\"title\":"; string_value(output, item.update.title);
        output << ",\"hardware_id\":"; string_value(output, item.update.driver_hardware_id);
        output << ",\"provider\":"; string_value(output, item.update.provider);
        output << ",\"driver_date\":" << item.update.driver_date_yyyymmdd
               << ",\"matching_devices\":"; string_array(output, item.update.matching_instance_ids);
        output << ",\"matching_present_devices\":";
        string_array(output, item.update.matching_present_instance_ids);
        output << ",\"status\":";
        string_value(output, open_driver::pipeline_status_name(item.status));
        output << ",\"explanation\":"; string_value(output, item.plain_explanation);
        output << ",\"selected_catalog_update_id\":";
        if (item.selected_catalog_update_id) string_value(output, *item.selected_catalog_update_id);
        else output << "null";
        output << ",\"catalog_candidates\":" << item.catalog_candidates
               << ",\"eligible_catalog_candidates\":" << item.eligible_catalog_candidates
               << ",\"safe_to_install\":false,\"error\":";
        if (item.error.empty()) output << "null";
        else string_value(output, item.error);
        output << '}';
    }
    output << (report.items.empty() ? "]" : "\n  ]")
           << ",\n  \"system_changes\":0\n}\n";
    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char** argv) {
    bool online = true;
    bool catalog_enabled = true;
    std::filesystem::path output_path;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--offline") online = false;
        else if (argument == "--skip-catalog") catalog_enabled = false;
        else if (argument == "--output" && index + 1 < argc) output_path = argv[++index];
        else {
            std::cerr << "usage: open_driver_pipeline --output <new-path> [--offline] [--skip-catalog]\n";
            return 2;
        }
    }
    if (output_path.empty()) {
        std::cerr << "--output is required\n";
        return 2;
    }
    if (std::filesystem::exists(output_path)) {
        std::cerr << "output path already exists: " << output_path.string() << '\n';
        return 4;
    }

    const auto inventory = open_driver::collect_device_inventory({.present_only = false});
    if (inventory.fatal_error) {
        std::cerr << inventory.fatal_error->operation << ": "
                  << inventory.fatal_error->message << '\n';
        return 1;
    }
    const auto updates = open_driver::search_windows_update_drivers(inventory.devices, online);
    if (updates.fatal_error) {
        std::cerr << updates.fatal_error->operation << ": "
                  << updates.fatal_error->message << '\n';
        return 1;
    }
    auto report = open_driver::start_pipeline(inventory.devices, updates.updates);
    if (catalog_enabled) {
        for (auto& item : report.items) {
            if (item.status == open_driver::PipelineStatus::catalog_lookup_pending) {
                resolve_catalog(item);
            }
        }
    }
    if (!write_report(output_path, report, online, catalog_enabled,
                      updates.operation_result_code)) {
        std::cerr << "could not create pipeline report\n";
        return 1;
    }
    const auto present = std::count_if(report.items.begin(), report.items.end(), [](const auto& item) {
        return item.status != open_driver::PipelineStatus::skipped_absent_device &&
               item.status != open_driver::PipelineStatus::no_device_match;
    });
    const auto selected = std::count_if(report.items.begin(), report.items.end(), [](const auto& item) {
        return item.status == open_driver::PipelineStatus::catalog_selected;
    });
    std::cout << "devices=" << report.inventory_devices
              << " present=" << report.present_devices
              << " offers=" << report.items.size()
              << " present_offers=" << present
              << " catalog_selected=" << selected
              << " changes=0 output=" << output_path.string() << '\n';
    return 0;
}

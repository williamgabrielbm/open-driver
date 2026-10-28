// Language: C++20; file: catalog_resolver_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>

#include "catalog_resolver.hpp"
#include "device_inventory.hpp"
#include "microsoft_catalog.hpp"
#include "windows_update_source.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, nullptr);
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

bool write_report(
    const std::filesystem::path& path,
    const open_driver::CatalogResolutionTarget& target,
    const open_driver::CatalogResolutionResult& result) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "{\n  \"schema_version\":1,\n  \"target\":{\"title\":";
    string_value(output, target.title);
    output << ",\"hardware_id\":"; string_value(output, target.hardware_id);
    output << ",\"architecture\":"; string_value(output, target.architecture);
    output << ",\"language\":"; string_value(output, target.language);
    output << ",\"provider\":"; string_value(output, target.provider);
    output << ",\"driver_date\":" << target.driver_date_yyyymmdd << "},\n  \"selected_catalog_update_id\":";
    if (result.selected_catalog_update_id) string_value(output, *result.selected_catalog_update_id);
    else output << "null";
    output << ",\n  \"ambiguous\":" << (result.ambiguous ? "true" : "false")
           << ",\n  \"candidates\":[";
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        const auto& candidate = result.candidates[index];
        output << (index == 0 ? "\n    {" : ",\n    {") << "\"catalog_update_id\":";
        string_value(output, candidate.search.catalog_update_id);
        output << ",\"title\":"; string_value(output, candidate.search.title);
        output << ",\"eligible\":" << (candidate.eligible ? "true" : "false")
               << ",\"score\":" << candidate.score << ",\"architectures\":";
        string_array(output, candidate.details.architectures);
        output << ",\"languages\":"; string_array(output, candidate.details.languages);
        output << ",\"hardware_ids\":"; string_array(output, candidate.details.hardware_ids);
        output << ",\"version\":"; string_value(output, candidate.details.version);
        output << ",\"version_date\":" << candidate.details.version_date_yyyymmdd
               << ",\"decision_trace\":";
        string_array(output, candidate.decision_trace);
        output << ",\"rejection_reasons\":";
        string_array(output, candidate.rejection_reasons);
        output << '}';
    }
    output << (result.candidates.empty() ? "]" : "\n  ]") << "\n}\n";
    return static_cast<bool>(output);
}

void usage() {
    std::cerr << "usage: open_driver_catalog_resolver "
                 "(--wua-update-id <guid> | --title <text> --hardware-id <id> [--date yyyymmdd] [--provider name]) "
                 "[--architecture name] [--language name] [--search-html path] [--details-dir path] --output path\n";
}

}  // namespace

int main(int argc, char** argv) {
    open_driver::CatalogResolutionTarget target;
    std::string wua_update_id;
    std::filesystem::path search_html_path;
    std::filesystem::path details_directory;
    std::filesystem::path output_path = L"catalog-resolution.json";

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--wua-update-id" && index + 1 < argc) wua_update_id = argv[++index];
        else if (argument == "--title" && index + 1 < argc) target.title = argv[++index];
        else if (argument == "--hardware-id" && index + 1 < argc) target.hardware_id = argv[++index];
        else if (argument == "--provider" && index + 1 < argc) target.provider = argv[++index];
        else if (argument == "--date" && index + 1 < argc) target.driver_date_yyyymmdd = std::stoul(argv[++index]);
        else if (argument == "--architecture" && index + 1 < argc) target.architecture = argv[++index];
        else if (argument == "--language" && index + 1 < argc) target.language = argv[++index];
        else if (argument == "--search-html" && index + 1 < argc) search_html_path = argv[++index];
        else if (argument == "--details-dir" && index + 1 < argc) details_directory = argv[++index];
        else if (argument == "--output" && index + 1 < argc) output_path = argv[++index];
        else { usage(); return 2; }
    }

    if (target.architecture.empty()) target.architecture = system_architecture();
    if (target.language.empty()) target.language = system_language();

    if (!wua_update_id.empty()) {
        const auto inventory = open_driver::collect_device_inventory({.present_only = false});
        if (inventory.fatal_error) {
            std::cerr << inventory.fatal_error->operation << ": " << inventory.fatal_error->message << '\n';
            return 1;
        }
        const auto updates = open_driver::search_windows_update_drivers(inventory.devices, true);
        if (updates.fatal_error) {
            std::cerr << updates.fatal_error->operation << ": " << updates.fatal_error->message << '\n';
            return 1;
        }
        const auto update = std::find_if(updates.updates.begin(), updates.updates.end(),
            [&](const auto& value) { return value.update_id == wua_update_id; });
        if (update == updates.updates.end()) {
            std::cerr << "WUA update ID was not found in the current search\n";
            return 1;
        }
        target.title = update->title;
        target.hardware_id = update->driver_hardware_id;
        target.provider = update->provider;
        target.driver_date_yyyymmdd = update->driver_date_yyyymmdd;
    }
    if (target.title.empty() || target.hardware_id.empty()) { usage(); return 2; }

    open_driver::CatalogSearchResult search;
    if (search_html_path.empty()) {
        search = open_driver::search_microsoft_update_catalog(target.title);
    } else {
        const auto html = read_file(search_html_path);
        if (html.empty()) { std::cerr << "could not read search HTML\n"; return 1; }
        search.entries = open_driver::parse_catalog_search_html(html);
    }
    if (search.error) {
        std::cerr << search.error->operation << ": " << search.error->message << '\n';
        return 1;
    }

    std::vector<open_driver::CatalogDetails> details;
    for (const auto& entry : search.entries) {
        if (details_directory.empty()) {
            const auto fetched = open_driver::get_microsoft_update_catalog_details(entry.catalog_update_id);
            if (fetched.error || !fetched.details) {
                std::cerr << "could not retrieve details for " << entry.catalog_update_id << '\n';
                return 1;
            }
            details.push_back(*fetched.details);
        } else {
            auto path = details_directory / (entry.catalog_update_id + ".html");
            if (!std::filesystem::exists(path)) path = details_directory / ("cat-" + entry.catalog_update_id + ".html");
            const auto html = read_file(path);
            const auto parsed = open_driver::parse_catalog_details_html(entry.catalog_update_id, html);
            if (!parsed) {
                std::cerr << "could not parse details for " << entry.catalog_update_id << '\n';
                return 1;
            }
            details.push_back(*parsed);
        }
    }

    const auto resolution = open_driver::resolve_catalog_candidates(target, search.entries, details);
    if (!write_report(output_path, target, resolution)) {
        std::cerr << "could not write report\n";
        return 1;
    }
    std::cout << "catalog_candidates=" << resolution.candidates.size()
              << " eligible=" << std::count_if(
                     resolution.candidates.begin(), resolution.candidates.end(),
                     [](const auto& candidate) { return candidate.eligible; })
              << " ambiguous=" << (resolution.ambiguous ? 1 : 0)
              << " selected=" << resolution.selected_catalog_update_id.value_or("")
              << '\n';
    return resolution.selected_catalog_update_id ? 0 : 3;
}

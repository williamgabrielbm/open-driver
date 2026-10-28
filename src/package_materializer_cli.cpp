// Language: C++20; file: package_materializer_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "device_inventory.hpp"
#include "external_package_source.hpp"
#include "local_driver_store.hpp"
#include "microsoft_catalog.hpp"
#include "package_materializer.hpp"

#include <algorithm>
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

std::string path_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool write_verified_marker(
    const std::filesystem::path& output_directory,
    const std::string& catalog_id,
    const std::vector<open_driver::CatalogFile>& files) {
    const auto marker_path = output_directory / L".open-driver-verified";
    if (std::filesystem::exists(marker_path)) return false;
    std::ofstream marker(marker_path, std::ios::binary | std::ios::out);
    if (!marker) return false;
    marker << "schema=1\n";
    marker << "catalog_update_id=" << catalog_id << '\n';
    for (const auto& file : files) {
        marker << "cab_sha256_base64=" << file.sha256_base64 << '\n';
    }
    return static_cast<bool>(marker);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool write_search_report(
    const std::filesystem::path& path,
    const std::string& query,
    const open_driver::CatalogSearchResult& result) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "{\n  \"schema_version\":1,\n  \"mode\":\"catalog_search\",\n  \"query\":";
    string_value(output, query);
    output << ",\n  \"entries\":[";
    for (std::size_t index = 0; index < result.entries.size(); ++index) {
        const auto& entry = result.entries[index];
        output << (index == 0 ? "\n    {" : ",\n    {") << "\"catalog_update_id\":";
        string_value(output, entry.catalog_update_id);
        output << ",\"title\":"; string_value(output, entry.title);
        output << ",\"products\":"; string_value(output, entry.products);
        output << ",\"classification\":"; string_value(output, entry.classification);
        output << ",\"last_updated\":"; string_value(output, entry.last_updated);
        output << ",\"version\":"; string_value(output, entry.version);
        output << ",\"size_bytes\":" << entry.size_bytes << '}';
    }
    output << (result.entries.empty() ? "]" : "\n  ]");
    if (result.error) {
        output << ",\n  \"error\":{\"operation\":";
        string_value(output, result.error->operation);
        output << ",\"windows_error\":" << result.error->windows_error << ",\"message\":";
        string_value(output, result.error->message);
        output << '}';
    }
    output << "\n}\n";
    return static_cast<bool>(output);
}

struct ExternalAnalysisSummary {
    std::string device_instance_id;
    std::string inf_path;
    std::size_t candidates{};
    std::uint32_t windows_rank{};
    std::uint32_t driver_date{};
    std::string driver_version;
    std::string provider;
    std::string description;
    std::string matching_id;
    std::string section_name;
    bool signature_valid{};
    std::string error;
};

bool write_package_report(
    const std::filesystem::path& path,
    const std::string& catalog_id,
    const open_driver::CatalogFileResult& metadata,
    const open_driver::PackageMaterializationResult* materialized,
    const std::vector<ExternalAnalysisSummary>& analysis) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "{\n  \"schema_version\":1,\n  \"mode\":\"catalog_package\",\n  \"catalog_update_id\":";
    string_value(output, catalog_id);
    output << ",\n  \"files\":[";
    for (std::size_t index = 0; index < metadata.files.size(); ++index) {
        const auto& file = metadata.files[index];
        output << (index == 0 ? "\n    {" : ",\n    {") << "\"file_name\":";
        string_value(output, file.file_name);
        output << ",\"url\":"; string_value(output, file.url);
        output << ",\"sha256_base64\":"; string_value(output, file.sha256_base64);
        output << ",\"sha1_base64\":"; string_value(output, file.digest_sha1_base64);
        output << '}';
    }
    output << (metadata.files.empty() ? "]" : "\n  ]");
    if (materialized != nullptr) {
        output << ",\n  \"materialization\":{\"downloads\":[";
        for (std::size_t index = 0; index < materialized->downloads.size(); ++index) {
            const auto& item = materialized->downloads[index];
            output << (index == 0 ? "{" : ",{") << "\"path\":";
            string_value(output, path_utf8(item.path));
            output << ",\"size_bytes\":" << item.size_bytes << ",\"sha256_base64\":";
            string_value(output, item.sha256_base64);
            output << ",\"hash_matches\":" << (item.hash_matches ? "true" : "false") << '}';
        }
        output << "],\"extracted_files\":" << materialized->extracted_files.size()
               << ",\"inf_files\":[";
        for (std::size_t index = 0; index < materialized->inf_files.size(); ++index) {
            const auto& inf = materialized->inf_files[index];
            output << (index == 0 ? "{" : ",{") << "\"path\":";
            string_value(output, path_utf8(inf.path));
            output << ",\"referenced_catalogs_present\":"
                   << (inf.referenced_catalogs_present ? "true" : "false") << '}';
        }
        output << "],\"catalogs\":[";
        for (std::size_t index = 0; index < materialized->catalogs.size(); ++index) {
            const auto& catalog = materialized->catalogs[index];
            output << (index == 0 ? "{" : ",{") << "\"path\":";
            string_value(output, path_utf8(catalog.path));
            output << ",\"signature_valid\":" << (catalog.valid ? "true" : "false")
                   << ",\"status\":";
            string_value(output, catalog.status);
            output << '}';
        }
        output << "]}";
    }
    output << ",\n  \"external_package_analysis\":[";
    for (std::size_t index = 0; index < analysis.size(); ++index) {
        const auto& item = analysis[index];
        output << (index == 0 ? "{" : ",{") << "\"device_instance_id\":";
        string_value(output, item.device_instance_id);
        output << ",\"inf_path\":"; string_value(output, item.inf_path);
        output << ",\"candidates\":" << item.candidates
               << ",\"windows_rank\":" << item.windows_rank
               << ",\"driver_date\":" << item.driver_date
               << ",\"driver_version\":"; string_value(output, item.driver_version);
        output << ",\"provider\":"; string_value(output, item.provider);
        output << ",\"description\":"; string_value(output, item.description);
        output << ",\"matching_id\":"; string_value(output, item.matching_id);
        output << ",\"section_name\":"; string_value(output, item.section_name);
        output << ",\"signature_valid\":" << (item.signature_valid ? "true" : "false")
               << ",\"error\":";
        string_value(output, item.error);
        output << '}';
    }
    output << "]\n}\n";
    return static_cast<bool>(output);
}

void usage() {
    std::cerr << "usage:\n"
              << "  open_driver_package --search <query> [--report <path>]\n"
              << "  open_driver_package --catalog-id <guid> --manifest-only [--report <path>]\n"
              << "  open_driver_package --catalog-id <guid> --output-dir <new-directory> "
                 "[--source-dir <verified-download-cache>] [--device <instance-id>] "
                 "[--report <path>]\n"
              << "  offline fixtures: --search-html <path> or --download-html <path>\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string search_query;
    std::string catalog_id;
    std::string device_instance_id;
    std::filesystem::path output_directory;
    std::filesystem::path report_path = L"catalog-package.json";
    std::filesystem::path source_directory;
    std::filesystem::path search_html_path;
    std::filesystem::path download_html_path;
    bool manifest_only = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--search" && index + 1 < argc) search_query = argv[++index];
        else if (argument == "--catalog-id" && index + 1 < argc) catalog_id = argv[++index];
        else if (argument == "--device" && index + 1 < argc) device_instance_id = argv[++index];
        else if (argument == "--output-dir" && index + 1 < argc) output_directory = argv[++index];
        else if (argument == "--source-dir" && index + 1 < argc) source_directory = argv[++index];
        else if (argument == "--report" && index + 1 < argc) report_path = argv[++index];
        else if (argument == "--search-html" && index + 1 < argc) search_html_path = argv[++index];
        else if (argument == "--download-html" && index + 1 < argc) download_html_path = argv[++index];
        else if (argument == "--manifest-only") manifest_only = true;
        else { usage(); return 2; }
    }

    if (!search_query.empty()) {
        if (!catalog_id.empty() || !output_directory.empty()) { usage(); return 2; }
        open_driver::CatalogSearchResult result;
        if (search_html_path.empty()) {
            result = open_driver::search_microsoft_update_catalog(search_query);
        } else {
            const auto html = read_file(search_html_path);
            if (html.empty()) {
                std::cerr << "could not read search HTML\n";
                return 1;
            }
            result.entries = open_driver::parse_catalog_search_html(html);
        }
        if (!write_search_report(report_path, search_query, result)) return 1;
        if (result.error) {
            std::cerr << result.error->operation << ": " << result.error->message << '\n';
            return 1;
        }
        std::cout << "catalog_candidates=" << result.entries.size()
                  << " report=" << path_utf8(report_path) << '\n';
        return 0;
    }

    if (catalog_id.empty() || (manifest_only != output_directory.empty())) {
        usage();
        return 2;
    }
    open_driver::CatalogFileResult metadata;
    if (download_html_path.empty()) {
        metadata = open_driver::get_microsoft_update_catalog_files(catalog_id);
    } else {
        const auto script = read_file(download_html_path);
        if (script.empty()) {
            std::cerr << "could not read download HTML\n";
            return 1;
        }
        metadata.files = open_driver::parse_catalog_download_script(script);
    }
    if (metadata.error) {
        std::cerr << metadata.error->operation << ": " << metadata.error->message << '\n';
        return 1;
    }
    if (manifest_only) {
        if (!write_package_report(report_path, catalog_id, metadata, nullptr, {})) return 1;
        std::cout << "catalog_files=" << metadata.files.size()
                  << " downloaded=0 report=" << path_utf8(report_path) << '\n';
        return 0;
    }

    const auto materialized = open_driver::materialize_catalog_package(
        metadata.files, output_directory,
        source_directory.empty()
            ? std::optional<std::filesystem::path>{}
            : std::optional<std::filesystem::path>{source_directory});
    std::vector<ExternalAnalysisSummary> analysis;
    if (!materialized.error && !device_instance_id.empty()) {
        const auto inventory = open_driver::collect_device_inventory({.present_only = false});
        const auto device = std::find_if(inventory.devices.begin(), inventory.devices.end(),
            [&](const auto& value) { return value.instance_id == device_instance_id; });
        if (device == inventory.devices.end()) {
            analysis.push_back({.device_instance_id = device_instance_id,
                                .error = "device instance not found"});
        } else {
            for (const auto& inf : materialized.inf_files) {
                const auto inf_path = path_utf8(inf.path);
                const auto result = open_driver::analyze_external_inf_for_device(*device, inf_path);
                ExternalAnalysisSummary summary{
                    .device_instance_id = device_instance_id,
                    .inf_path = inf_path,
                    .candidates = result.candidates.size(),
                    .error = result.fatal_error ? result.fatal_error->message : std::string{},
                };
                if (!result.candidates.empty()) {
                    const auto& candidate = result.candidates.front();
                    summary.windows_rank = candidate.windows_rank;
                    summary.driver_date = candidate.driver_date;
                    summary.provider = candidate.provider;
                    summary.description = candidate.description;
                    summary.matching_id = candidate.matching_id;
                    summary.section_name = candidate.section_name;
                    summary.signature_valid = candidate.signature_valid;
                    std::ostringstream version;
                    for (std::size_t part = 0; part < candidate.driver_version.parts.size(); ++part) {
                        if (part != 0) version << '.';
                        version << candidate.driver_version.parts[part];
                    }
                    summary.driver_version = version.str();
                }
                analysis.push_back(std::move(summary));
            }
        }
    }
    if (!write_package_report(report_path, catalog_id, metadata, &materialized, analysis)) return 1;
    if (materialized.error) {
        std::cerr << materialized.error->operation << ": " << materialized.error->message << '\n';
        return 1;
    }
    if (!write_verified_marker(output_directory, catalog_id, metadata.files)) {
        std::cerr << "could not create verified package marker\n";
        return 1;
    }
    std::cout << "downloads=" << materialized.downloads.size()
              << " extracted=" << materialized.extracted_files.size()
              << " infs=" << materialized.inf_files.size()
              << " catalogs=" << materialized.catalogs.size()
              << " external_analyses=" << analysis.size()
              << " report=" << path_utf8(report_path) << '\n';
    return 0;
}

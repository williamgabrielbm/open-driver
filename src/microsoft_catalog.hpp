// Language: C++20; file: microsoft_catalog.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace open_driver {

struct CatalogError {
    std::string operation;
    std::uint32_t windows_error{};
    std::string message;
};

struct CatalogSearchEntry {
    std::string catalog_update_id;
    std::string title;
    std::string products;
    std::string classification;
    std::string last_updated;
    std::string version;
    std::uint64_t size_bytes{};
};

struct CatalogFile {
    std::string url;
    std::string file_name;
    std::string digest_sha1_base64;
    std::string sha256_base64;
};

struct CatalogDetails {
    std::string catalog_update_id;
    std::string description;
    std::vector<std::string> architectures;
    std::vector<std::string> products;
    std::vector<std::string> languages;
    std::string company;
    std::string manufacturer;
    std::string driver_class;
    std::string model;
    std::string provider;
    std::string version;
    std::uint32_t version_date_yyyymmdd{};
    std::vector<std::string> hardware_ids;
};

struct CatalogSearchResult {
    std::vector<CatalogSearchEntry> entries;
    std::optional<CatalogError> error;
};

struct CatalogFileResult {
    std::vector<CatalogFile> files;
    std::optional<CatalogError> error;
};

struct CatalogDetailsResult {
    std::optional<CatalogDetails> details;
    std::optional<CatalogError> error;
};

struct CatalogDownloadResult {
    std::filesystem::path path;
    std::uint64_t size_bytes{};
    std::string sha256_base64;
    bool hash_matches{};
    std::optional<CatalogError> error;
};

struct HttpsTextResult {
    std::string body;
    std::optional<CatalogError> error;
};

[[nodiscard]] HttpsTextResult fetch_https_text(const std::string& url);

[[nodiscard]] CatalogSearchResult search_microsoft_update_catalog(
    const std::string& query);

[[nodiscard]] std::vector<CatalogSearchEntry> parse_catalog_search_html(
    std::string_view html);

[[nodiscard]] CatalogDetailsResult get_microsoft_update_catalog_details(
    const std::string& catalog_update_id);

[[nodiscard]] std::optional<CatalogDetails> parse_catalog_details_html(
    const std::string& catalog_update_id,
    std::string_view html);

[[nodiscard]] CatalogFileResult get_microsoft_update_catalog_files(
    const std::string& catalog_update_id);

[[nodiscard]] std::vector<CatalogFile> parse_catalog_download_script(
    std::string_view script);

[[nodiscard]] CatalogDownloadResult download_catalog_file(
    const CatalogFile& file,
    const std::filesystem::path& destination,
    bool accept_computed_hash = false);

[[nodiscard]] std::string sha256_file_base64(const std::filesystem::path& path);

}  // namespace open_driver

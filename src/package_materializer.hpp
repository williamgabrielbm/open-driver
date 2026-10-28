// Language: C++20; file: package_materializer.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "microsoft_catalog.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace open_driver {

struct CatalogSignatureRecord {
    std::filesystem::path path;
    bool valid{};
    std::string status;
};

struct InfPackageRecord {
    std::filesystem::path path;
    std::vector<std::string> catalog_file_names;
    bool referenced_catalogs_present{};
};

struct PackageMaterializationResult {
    std::vector<CatalogDownloadResult> downloads;
    std::vector<std::filesystem::path> extracted_files;
    std::vector<InfPackageRecord> inf_files;
    std::vector<CatalogSignatureRecord> catalogs;
    std::optional<CatalogError> error;
};

[[nodiscard]] bool is_safe_cabinet_relative_path(const std::wstring& value);

[[nodiscard]] bool verify_catalog_signature_file(
    const std::filesystem::path& path,
    std::string& status);

[[nodiscard]] PackageMaterializationResult materialize_catalog_package(
    const std::vector<CatalogFile>& files,
    const std::filesystem::path& output_directory,
    const std::optional<std::filesystem::path>& source_directory = std::nullopt,
    bool accept_computed_archive_hash = false);

}  // namespace open_driver

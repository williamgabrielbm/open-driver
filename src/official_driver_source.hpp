// Language: C++20; file: official_driver_source.hpp; target: Windows 10/11; runtime: MSVC/CMake.
#pragma once

#include "device_inventory.hpp"
#include "microsoft_catalog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace open_driver {

struct MachineIdentity {
    std::string manufacturer;
    std::string product_name;
    std::string family;
    std::string sku;
};

struct OfficialDriverPackage {
    std::string id;
    std::string source_name;
    std::string model;
    CatalogFile file;
    std::string driver_version;
    std::uint32_t published_date_yyyymmdd{};
    std::uint64_t size_bytes{};
    std::vector<std::string> hardware_id_prefixes;
    std::uint32_t minimum_windows_build{};
    std::uint32_t maximum_windows_build{};
};

struct OfficialSourceRefresh {
    std::vector<OfficialDriverPackage> packages;
    std::vector<std::string> errors;
    std::size_t checks_required{};
    std::size_t checks_completed{};
};

[[nodiscard]] MachineIdentity current_machine_identity();

[[nodiscard]] std::vector<OfficialDriverPackage> official_packages_for_machine(
    const MachineIdentity& identity);

[[nodiscard]] OfficialSourceRefresh refresh_official_packages(
    const std::vector<DeviceRecord>& inventory);

[[nodiscard]] std::optional<OfficialDriverPackage> parse_intel_graphics_download_page(
    std::string_view html);

[[nodiscard]] std::optional<OfficialDriverPackage> parse_intel_i226_download_pages(
    std::string_view download_html,
    std::string_view release_notes_html);

[[nodiscard]] bool official_package_matches_device(
    const OfficialDriverPackage& package,
    const DeviceRecord& device);

[[nodiscard]] bool official_package_is_newer_than_installed(
    const OfficialDriverPackage& package,
    const DeviceRecord& device);

[[nodiscard]] bool official_package_supports_windows(
    const OfficialDriverPackage& package,
    std::uint32_t windows_build);

}  // namespace open_driver

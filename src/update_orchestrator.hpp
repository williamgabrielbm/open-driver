// Language: C++20; file: update_orchestrator.hpp; target: Windows 10/11; runtime: MSVC/CMake.
#pragma once

#include "device_inventory.hpp"
#include "microsoft_catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace open_driver {

struct WindowsTarget {
    std::uint32_t build{};
    bool windows_11{};
    std::string architecture;
};

struct PreparedUpdate {
    std::filesystem::path package_directory;
    std::filesystem::path plan_path;
    std::filesystem::path journal_path;
    std::string plan_id;
};

struct UpdateOffer {
    DeviceRecord device;
    CatalogSearchEntry search;
    CatalogDetails details;
    std::string source_name{"Microsoft Update Catalog"};
    std::string source_package_id;
    std::vector<CatalogFile> source_files;
    bool source_hash_is_published{true};
    std::string available_version;
    std::uint32_t available_date_yyyymmdd{};
    std::uint64_t estimated_download_bytes{};
    std::size_t related_devices{1};
    std::optional<PreparedUpdate> prepared;
};

struct UpdateDiscoveryReport {
    WindowsTarget target;
    bool complete{};
    bool cancelled{};
    std::size_t devices_detected{};
    std::size_t devices_checked{};
    std::size_t packages_checked{};
    std::size_t source_checks_required{};
    std::size_t source_checks_completed{};
    std::size_t candidates_validated{};
    std::size_t candidates_rejected{};
    std::size_t windows_update_candidates{};
    std::size_t windows_update_present_candidates{};
    std::vector<UpdateOffer> offers;
    std::vector<std::string> errors;
    std::vector<std::string> rejections;
};

struct UpdateAttemptPaths {
    std::filesystem::path plan;
    std::filesystem::path journal;
};

struct PrepareUpdateResult {
    std::optional<PreparedUpdate> prepared;
    std::string available_version;
    std::uint32_t available_date_yyyymmdd{};
    bool rejected{};
    std::string error;
};

using DiscoveryProgress = std::function<void(std::size_t current, std::size_t total,
                                              const std::string& device_name)>;

[[nodiscard]] WindowsTarget current_windows_target();

[[nodiscard]] bool device_snapshot_matches_offer(
    const UpdateOffer& offer,
    const DeviceRecord& current_device);

[[nodiscard]] bool driver_component_is_scannable(const DeviceRecord& device);

[[nodiscard]] UpdateAttemptPaths update_attempt_paths(
    const std::filesystem::path& data_root,
    const std::string& plan_id,
    const std::string& attempt_token);

[[nodiscard]] bool catalog_product_matches_windows(
    const std::string& products,
    const WindowsTarget& target);

[[nodiscard]] std::optional<std::string> catalog_entry_version(
    const CatalogSearchEntry& entry);

[[nodiscard]] UpdateDiscoveryReport discover_updates(
    const std::vector<DeviceRecord>& inventory,
    const DiscoveryProgress& progress = {},
    std::stop_token stop = {});

[[nodiscard]] PrepareUpdateResult prepare_update(
    const UpdateOffer& offer,
    const std::filesystem::path& application_data_root,
    std::stop_token stop = {});

void validate_and_prepare_offers(
    UpdateDiscoveryReport& report,
    const std::filesystem::path& application_data_root,
    const DiscoveryProgress& progress = {},
    std::stop_token stop = {});

}  // namespace open_driver

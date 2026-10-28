// Language: C++20; file: pipeline.hpp; target: Windows 11; runtime: MSVC/CMake.
#pragma once

#include "catalog_resolver.hpp"
#include "windows_update_source.hpp"

#include <optional>
#include <string>
#include <vector>

namespace open_driver {

enum class PipelineStatus {
    no_device_match,
    skipped_absent_device,
    catalog_lookup_pending,
    catalog_lookup_failed,
    catalog_ambiguous,
    catalog_selected,
};

struct PipelineItem {
    WindowsUpdateDriverRecord update;
    PipelineStatus status{PipelineStatus::no_device_match};
    std::string plain_explanation;
    std::optional<std::string> selected_catalog_update_id;
    std::size_t catalog_candidates{};
    std::size_t eligible_catalog_candidates{};
    std::string error;
};

struct PipelineReport {
    std::size_t inventory_devices{};
    std::size_t present_devices{};
    std::vector<PipelineItem> items;
};

[[nodiscard]] PipelineReport start_pipeline(
    const std::vector<DeviceRecord>& inventory,
    const std::vector<WindowsUpdateDriverRecord>& updates);

void apply_catalog_resolution(
    PipelineItem& item,
    const CatalogResolutionResult& resolution);

void apply_catalog_error(PipelineItem& item, std::string error);

[[nodiscard]] std::string pipeline_status_name(PipelineStatus status);

}  // namespace open_driver

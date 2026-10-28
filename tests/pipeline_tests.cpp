// Language: C++20; file: pipeline_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "pipeline.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::vector<open_driver::DeviceRecord> inventory = {
        {.instance_id = "PRESENT", .present = true},
        {.instance_id = "OLD", .present = false},
    };
    const std::vector<open_driver::WindowsUpdateDriverRecord> updates = {
        {.update_id = "present", .matching_instance_ids = {"PRESENT"},
         .matching_present_instance_ids = {"PRESENT"}},
        {.update_id = "old", .matching_instance_ids = {"OLD"}},
        {.update_id = "unknown"},
    };
    auto report = open_driver::start_pipeline(inventory, updates);
    require(report.inventory_devices == 2 && report.present_devices == 1,
            "pipeline must count present and historical devices");
    require(report.items[0].status == open_driver::PipelineStatus::catalog_lookup_pending,
            "present device must advance to catalog lookup");
    require(report.items[1].status == open_driver::PipelineStatus::skipped_absent_device,
            "absent device must be skipped");
    require(report.items[2].status == open_driver::PipelineStatus::no_device_match,
            "unmatched offer must be isolated");

    open_driver::CatalogResolutionResult selected{
        .candidates = {{.eligible = true, .score = 100}},
        .selected_catalog_update_id = "catalog-id",
    };
    open_driver::apply_catalog_resolution(report.items[0], selected);
    require(report.items[0].status == open_driver::PipelineStatus::catalog_selected &&
                report.items[0].selected_catalog_update_id == "catalog-id",
            "unique catalog result must advance the item");

    selected.selected_catalog_update_id.reset();
    selected.ambiguous = true;
    open_driver::apply_catalog_resolution(report.items[0], selected);
    require(report.items[0].status == open_driver::PipelineStatus::catalog_ambiguous,
            "catalog tie must stay visible and unselected");

    open_driver::apply_catalog_error(report.items[0], "network error");
    require(report.items[0].status == open_driver::PipelineStatus::catalog_lookup_failed &&
                report.items[0].error == "network error",
            "catalog failure must be recorded without advancing");
    std::cout << "pipeline_tests: passed\n";
    return 0;
}

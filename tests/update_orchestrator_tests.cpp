// Language: C++20; file: update_orchestrator_tests.cpp; target: Windows 10/11; runtime: MSVC/CMake.
#include "update_orchestrator.hpp"
#include "official_driver_source.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    open_driver::UpdateDiscoveryReport incomplete_report;
    require(!incomplete_report.complete,
            "a discovery report must start incomplete until every source answers");
    std::stop_source cancelled_source;
    cancelled_source.request_stop();
    const auto cancelled_report = open_driver::discover_updates(
        {}, {}, cancelled_source.get_token());
    require(cancelled_report.cancelled && !cancelled_report.complete &&
                cancelled_report.offers.empty(),
            "a pre-cancelled discovery must stop before querying update sources");
    const auto cancelled_prepare = open_driver::prepare_update(
        {}, std::filesystem::temp_directory_path(), cancelled_source.get_token());
    require(!cancelled_prepare.prepared && cancelled_prepare.error == "operação cancelada",
            "a pre-cancelled preparation must not create or download package data");
    const open_driver::WindowsTarget win10{.build = 19045, .windows_11 = false,
                                           .architecture = "AMD64"};
    const open_driver::WindowsTarget win11{.build = 26100, .windows_11 = true,
                                           .architecture = "AMD64"};
    require(open_driver::catalog_product_matches_windows(
                "Windows 10, Vibranium and later, Servicing Drivers", win10),
            "Windows 10 products should match Windows 10");
    require(!open_driver::catalog_product_matches_windows(
                "Windows 11 Client, version 24H2 and later", win10),
            "Windows 11-only products must not match Windows 10");
    require(!open_driver::catalog_product_matches_windows(
                "Windows - Server, version 21H2 and later", win10),
            "Server packages must be rejected");
    require(open_driver::catalog_product_matches_windows(
                "Windows 11 Client, version 24H2 and later", win11),
            "Windows 11 products should match Windows 11");
    require(open_driver::catalog_entry_version({.title = "Intel - System - 30.100.2413.49",
                                                 .version = "n/a"}) == "30.100.2413.49",
            "version should be extracted from the catalog title");
    const auto first_attempt = open_driver::update_attempt_paths(
        "C:\\OpenDriver", "odc-fixed-plan", "1000");
    const auto retry_attempt = open_driver::update_attempt_paths(
        "C:\\OpenDriver", "odc-fixed-plan", "2000");
    require(first_attempt.plan != retry_attempt.plan &&
                first_attempt.journal != retry_attempt.journal,
            "a retry of the same plan must use new plan and journal files");
    require(first_attempt.plan.filename() == "odc-fixed-plan-1000.json" &&
                first_attempt.journal.filename() == "odc-fixed-plan-1000.jsonl",
            "attempt token must be part of both audit artifact names");
    open_driver::UpdateOffer snapshot_offer;
    snapshot_offer.device.instance_id = "PCI\\VEN_8086&DEV_A082\\INSTANCE";
    snapshot_offer.device.installed_driver = {
        .provider = "INTEL", .version = "10.1.24.6", .inf_path = "oem36.inf",
        .date_yyyymmdd = 19680718};
    auto current_device = snapshot_offer.device;
    current_device.present = true;
    require(open_driver::device_snapshot_matches_offer(snapshot_offer, current_device),
            "unchanged installed snapshot must remain executable");
    current_device.installed_driver.version = "10.1.24.7";
    current_device.installed_driver.inf_path = "oem45.inf";
    require(!open_driver::device_snapshot_matches_offer(snapshot_offer, current_device),
            "an already updated device must make the old offer stale");
    open_driver::DeviceRecord physical_device{
        .instance_id = "PCI\\VEN_8086&DEV_1234\\INSTANCE",
        .class_name = "System",
        .hardware_ids = {"PCI\\VEN_8086&DEV_1234"},
        .installed_driver = {
            .provider = "Microsoft", .version = "10.0.19041.1",
            .inf_path = "machine.inf"},
        .present = true,
    };
    require(open_driver::driver_component_is_scannable(physical_device),
            "physical hardware using an inbox Microsoft driver must be scanned");
    auto software_component = physical_device;
    software_component.class_name = "SoftwareComponent";
    require(open_driver::driver_component_is_scannable(software_component),
            "driver-backed software components with hardware IDs must be scanned");
    software_component.class_name = "SoftwareDevice";
    require(!open_driver::driver_component_is_scannable(software_component),
            "pure software devices must not inflate the hardware scan");
    auto virtual_device = physical_device;
    virtual_device.instance_id = "ROOT\\VIRTUAL_DEVICE\\0000";
    require(!open_driver::driver_component_is_scannable(virtual_device),
            "virtual root devices must not be presented as physical components");
    const auto official = open_driver::official_packages_for_machine({
        .manufacturer = "GMKtec", .product_name = "NucBox_M2Pro_S",
        .family = "M2Pro_S", .sku = "M2Pro_S-001"});
    require(official.size() == 9,
            "the GMKtec M2 Pro S must expose OEM, Intel, and vendor catalog packages");
    require(official.front().driver_version == "6001.15.159.0",
            "official manifests must expose the actual driver version before download");
    const auto live_graphics = open_driver::parse_intel_graphics_download_page(R"(
        <p>This download installs Intel Graphics Driver 32.0.101.7092.</p>
        <a href="https://downloadmirror.intel.com/929468/gfx_win_101.7092.exe">Download</a>
        <div>Size: 744.7 MB</div><div>Date: 9/25/2026</div>)");
    require(live_graphics && live_graphics->driver_version == "32.0.101.7092" &&
                live_graphics->published_date_yyyymmdd == 20260925 &&
                live_graphics->size_bytes == 744'700'000,
            "the live Intel graphics page must produce a future-proof package descriptor");
    const auto live_ethernet = open_driver::parse_intel_i226_download_pages(R"(
        <a href="https://downloadmirror.intel.com/930885/Release_31.3.zip">Release_31.3.zip</a>
        <div>Size: 1.4 GB</div>)", R"(
        <table><tr><td>e2fn</td><td>2.1.5.12</td><td>1.1.4.46</td></tr></table>)");
    require(live_ethernet && live_ethernet->driver_version == "1.1.4.46" &&
                live_ethernet->file.file_name == "Release_31.3.zip" &&
                live_ethernet->size_bytes == 1'400'000'000,
            "the live Intel Ethernet pages must resolve the current Windows 10 I226 driver");
    auto wifi = physical_device;
    wifi.hardware_ids = {"PCI\\VEN_10EC&DEV_B852&SUBSYS_B85210EC"};
    require(open_driver::official_package_matches_device(official.front(), wifi),
            "the official Wi-Fi package must match the installed Realtek adapter");
    wifi.hardware_ids = {"PCI\\VEN_8086&DEV_125C&SUBSYS_00008086"};
    require(!open_driver::official_package_matches_device(official.front(), wifi),
            "the Wi-Fi package must not be offered to the Ethernet controller");
    const auto intel_i226 = std::find_if(official.begin(), official.end(), [](const auto& item) {
        return item.id == "intel-i226-release-31.3-20261019";
    });
    require(intel_i226 != official.end() &&
                intel_i226->file.file_name == "Release_31.3.zip" &&
                intel_i226->driver_version == "1.1.4.46",
            "the Intel I226 offer must point at release 31.3, which contains 1.1.4.46");
    const auto catalog_wifi = std::find_if(official.begin(), official.end(), [](const auto& item) {
        return item.id == "microsoft-realtek-8852be-win10-6001.15.165.3";
    });
    require(catalog_wifi != official.end() && catalog_wifi->size_bytes == 2'477'980 &&
                catalog_wifi->driver_version == "6001.15.165.3" &&
                catalog_wifi->published_date_yyyymmdd == 20260713,
            "the Realtek update must use the small Microsoft Catalog CAB");
    require(open_driver::official_package_supports_windows(*catalog_wifi, 19045),
            "the Realtek Catalog package must support Windows 10 22H2");
    require(!open_driver::official_package_supports_windows(*catalog_wifi, 26100),
            "the Windows 10 Realtek package must not be offered on Windows 11");
    const auto serial_package = std::find_if(official.begin(), official.end(), [](const auto& item) {
        return item.id == "hp-intel-serial-io-sp165734";
    });
    require(serial_package != official.end(),
            "the vendor catalog must include the multi-device Serial IO package");
    auto serial = physical_device;
    serial.hardware_ids = {"PCI\\VEN_8086&DEV_A0E8&SUBSYS_00000000"};
    serial.installed_driver.version = "30.100.2129.8";
    serial.installed_driver.date_yyyymmdd = 20210713;
    require(open_driver::official_package_matches_device(*serial_package, serial),
            "the Serial IO package must match every covered controller by hardware ID");
    require(open_driver::official_package_is_newer_than_installed(*serial_package, serial),
            "a newer INF date must allow a legitimate vendor version-branch change");
    serial.installed_driver.date_yyyymmdd = 20250101;
    require(!open_driver::official_package_is_newer_than_installed(*serial_package, serial),
            "an older vendor package date must not be offered as an update");
    std::cout << "update orchestrator tests passed\n";
    return 0;
}

// Language: C++20; file: package_materializer_tests.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "microsoft_catalog.hpp"
#include "package_materializer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    require(open_driver::is_safe_cabinet_relative_path(L"driver\\device.inf"),
            "normal cabinet path must be accepted");
    require(!open_driver::is_safe_cabinet_relative_path(L"..\\outside.inf"),
            "parent traversal must be rejected");
    require(!open_driver::is_safe_cabinet_relative_path(L"C:\\outside.inf"),
            "rooted path must be rejected");
    require(!open_driver::is_safe_cabinet_relative_path(L"folder\\..\\outside.inf"),
            "nested parent traversal must be rejected");

    const auto fixture = std::filesystem::temp_directory_path() / L"open-driver-sha256-fixture.txt";
    {
        std::ofstream output(fixture, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    require(open_driver::sha256_file_base64(fixture) ==
                "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=",
            "SHA-256 must match the known abc vector");

    const std::string search_html = R"html(
      <tr id="d29470d5-8699-4bd3-9518-651acbb78474_R2">
        <td></td><td><a>HP - LCD - HP V206hz LED Backlit Monitor</a></td>
        <td>Windows 8.1 and later drivers</td><td>Drivers (Other Hardware)</td>
        <td>11/17/2013</td><td>n/a</td>
        <td><span>23 KB</span><span id="d29470d5_originalSize">23650</span></td>
      </tr>)html";
    const auto entries = open_driver::parse_catalog_search_html(search_html);
    require(entries.size() == 1, "catalog search row must parse");
    require(entries[0].catalog_update_id == "d29470d5-8699-4bd3-9518-651acbb78474",
            "catalog update ID must parse");
    require(entries[0].size_bytes == 23650, "catalog original byte size must parse");

    const std::string details_html = R"html(
      <div id="archDiv">Architecture: AMD64</div>
      <span id="ScopedViewHandler_version">2.1.5.12</span>
      <span id="ScopedViewHandler_versionDate">3/24/2026</span>
      <div id="driverhwIDs">PCI\VEN_8086&amp;DEV_125C<br/>PCI\VEN_8086&amp;DEV_125C&amp;SUBSYS_00008086</div>
      <span id="ScopedViewHandler_labelSupportedUpdateIDs"></span>)html";
    const auto details = open_driver::parse_catalog_details_html("catalog-id", details_html);
    require(details.has_value(), "catalog detail metadata must parse");
    require(details->hardware_ids.size() == 2,
            "all catalog hardware IDs must parse");
    require(details->hardware_ids[0] == "PCI\\VEN_8086&DEV_125C",
            "HTML entities in hardware IDs must be decoded before parsing");

    const std::string download_script = R"script(
      downloadInformation[0].files[0].url = 'https://catalog.s.download.windowsupdate.com/package.cab';
      downloadInformation[0].files[0].digest = '93/4j3oO+czLljyhY9iqlO2C4OQ=';
      downloadInformation[0].files[0].sha256 = '3SRLGGm/MWr52Hs9tYFZ8oycVmlzWGWCW5OP0Qyzz2I=';
      downloadInformation[0].files[0].fileName = 'package.cab';)script";
    const auto files = open_driver::parse_catalog_download_script(download_script);
    require(files.size() == 1, "catalog download metadata must parse");
    require(files[0].file_name == "package.cab", "catalog file name must parse");
    require(files[0].sha256_base64 == "3SRLGGm/MWr52Hs9tYFZ8oycVmlzWGWCW5OP0Qyzz2I=",
            "catalog SHA-256 must parse");
    std::error_code error;
    std::filesystem::remove(fixture, error);
    std::cout << "package_materializer_tests: passed\n";
    return 0;
}

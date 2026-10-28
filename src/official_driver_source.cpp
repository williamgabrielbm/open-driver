// Language: C++20; file: official_driver_source.cpp; target: Windows 10/11; runtime: MSVC/CMake.
#include <windows.h>

#include "official_driver_source.hpp"

#include "recommendation_engine.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace open_driver {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string registry_string(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, &type, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::vector<std::string> device_identifiers(const DeviceRecord& device) {
    std::vector<std::string> values = device.hardware_ids;
    values.insert(values.end(), device.compatible_ids.begin(), device.compatible_ids.end());
    if (!device.installed_driver.matching_device_id.empty()) {
        values.push_back(device.installed_driver.matching_device_id);
    }
    for (auto& value : values) value = lower_ascii(std::move(value));
    return values;
}

std::string normalized_intel_html(std::string_view html) {
    std::string result(html);
    for (std::size_t position = 0; (position = result.find("\\/", position)) != std::string::npos;) {
        result.replace(position, 2, "/");
        ++position;
    }
    for (std::size_t position = 0; (position = result.find("&amp;", position)) != std::string::npos;) {
        result.replace(position, 5, "&");
        ++position;
    }
    return result;
}

std::string html_text(std::string_view html) {
    auto value = normalized_intel_html(html);
    value = std::regex_replace(value, std::regex("<[^>]+>"), " ");
    value = std::regex_replace(value, std::regex("\\s+"), " ");
    return value;
}

std::optional<std::string> first_regex_group(
    const std::string& value,
    const std::string& pattern) {
    std::smatch match;
    if (!std::regex_search(value, match, std::regex(pattern, std::regex::icase)) ||
        match.size() < 2) {
        return std::nullopt;
    }
    return match[1].str();
}

std::uint64_t intel_size_bytes(const std::string& text) {
    std::smatch match;
    if (!std::regex_search(text, match,
            std::regex(R"(Size\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*(MB|GB))",
                       std::regex::icase))) {
        return 0;
    }
    const auto value = std::stod(match[1].str());
    const auto unit = lower_ascii(match[2].str());
    const auto multiplier = unit == "gb" ? 1'000'000'000.0 : 1'000'000.0;
    return static_cast<std::uint64_t>(value * multiplier);
}

std::uint32_t intel_date(const std::string& text) {
    std::smatch match;
    if (!std::regex_search(text, match,
            std::regex(R"(Date\s*:\s*([0-9]{1,2})/([0-9]{1,2})/([0-9]{4}))",
                       std::regex::icase))) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::stoul(match[3].str()) * 10000U +
                                      std::stoul(match[1].str()) * 100U +
                                      std::stoul(match[2].str()));
}

bool inventory_has_hardware_prefix(
    const std::vector<DeviceRecord>& inventory,
    const std::string& prefix) {
    const auto wanted = lower_ascii(prefix);
    for (const auto& device : inventory) {
        const auto identifiers = device_identifiers(device);
        if (std::any_of(identifiers.begin(), identifiers.end(), [&](const auto& value) {
                return value.starts_with(wanted);
            })) {
            return true;
        }
    }
    return false;
}

std::string source_error(const std::string& name, const CatalogError& error) {
    return name + ": " + error.operation + ": " + error.message;
}

OfficialDriverPackage package(
    std::string id,
    std::string file_name,
    std::string url,
    std::string driver_version,
    std::uint64_t size_bytes,
    std::vector<std::string> hardware_ids) {
    return {
        .id = std::move(id),
        .source_name = "GMKtec (oficial)",
        .model = "M2 Pro S",
        .file = {.url = std::move(url), .file_name = std::move(file_name)},
        .driver_version = std::move(driver_version),
        // The OEM archive publication date is not the DriverVer date of every
        // INF inside it. Keep it unknown so version comparison is used until
        // the downloaded INF supplies its real date.
        .published_date_yyyymmdd = 0,
        .size_bytes = size_bytes,
        .hardware_id_prefixes = std::move(hardware_ids),
    };
}

OfficialDriverPackage intel_package(
    std::string id,
    std::string model,
    std::string file_name,
    std::string url,
    std::string sha256_base64,
    std::string driver_version,
    std::uint32_t date,
    std::uint64_t size_bytes,
    std::vector<std::string> hardware_ids) {
    return {
        .id = std::move(id),
        .source_name = "Intel (oficial)",
        .model = std::move(model),
        .file = {.url = std::move(url), .file_name = std::move(file_name),
                 .sha256_base64 = std::move(sha256_base64)},
        .driver_version = std::move(driver_version),
        .published_date_yyyymmdd = date,
        .size_bytes = size_bytes,
        .hardware_id_prefixes = std::move(hardware_ids),
    };
}

OfficialDriverPackage vendor_package(
    std::string id,
    std::string source_name,
    std::string model,
    std::string file_name,
    std::string url,
    std::string driver_version,
    std::uint32_t date,
    std::uint64_t size_bytes,
    std::vector<std::string> hardware_ids,
    std::string sha256_base64 = {},
    std::uint32_t minimum_windows_build = 0,
    std::uint32_t maximum_windows_build = 0) {
    return {
        .id = std::move(id),
        .source_name = std::move(source_name),
        .model = std::move(model),
        .file = {.url = std::move(url), .file_name = std::move(file_name),
                 .sha256_base64 = std::move(sha256_base64)},
        .driver_version = std::move(driver_version),
        .published_date_yyyymmdd = date,
        .size_bytes = size_bytes,
        .hardware_id_prefixes = std::move(hardware_ids),
        .minimum_windows_build = minimum_windows_build,
        .maximum_windows_build = maximum_windows_build,
    };
}

}  // namespace

MachineIdentity current_machine_identity() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    MachineIdentity identity{
        .manufacturer = registry_string(key, L"SystemManufacturer"),
        .product_name = registry_string(key, L"SystemProductName"),
        .family = registry_string(key, L"SystemFamily"),
        .sku = registry_string(key, L"SystemSKU"),
    };
    RegCloseKey(key);
    return identity;
}

std::optional<OfficialDriverPackage> parse_intel_graphics_download_page(
    std::string_view html) {
    const auto normalized = normalized_intel_html(html);
    const auto text = html_text(normalized);
    const auto url = first_regex_group(
        normalized,
        R"((https://downloadmirror\.intel\.com/[0-9]+/gfx_win_[A-Za-z0-9_.%-]+\.exe))");
    const auto version = first_regex_group(
        text,
        R"((?:Graphics Driver|version)[^0-9]{0,80}([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))");
    if (!url || !version) return std::nullopt;
    const auto slash = url->find_last_of('/');
    if (slash == std::string::npos || slash + 1 == url->size()) return std::nullopt;
    return vendor_package(
        "intel-graphics-live-" + *version,
        "Intel Download Center (consulta ao vivo)",
        "Intel 11th-14th Gen Graphics",
        url->substr(slash + 1), *url, *version, intel_date(text), intel_size_bytes(text),
        {"PCI\\VEN_8086&DEV_9A49"}, {}, 19045, 0);
}

std::optional<OfficialDriverPackage> parse_intel_i226_download_pages(
    std::string_view download_html,
    std::string_view release_notes_html) {
    const auto normalized = normalized_intel_html(download_html);
    const auto text = html_text(normalized);
    const auto url = first_regex_group(
        normalized,
        R"((https://downloadmirror\.intel\.com/[0-9]+/Release_[0-9.]+\.zip))");
    const auto release = first_regex_group(text, R"(Release_([0-9]+(?:\.[0-9]+)+)\.zip)");
    if (!url || !release) return std::nullopt;

    const auto notes = normalized_intel_html(release_notes_html);
    std::smatch row_match;
    if (!std::regex_search(notes, row_match,
            std::regex(R"(<tr[^>]*>[\s\S]*?e2fn[\s\S]*?</tr>)", std::regex::icase))) {
        return std::nullopt;
    }
    const auto row = html_text(row_match[0].str());
    const std::regex version_expression(R"(([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))");
    std::optional<std::string> version;
    for (auto iterator = std::sregex_iterator(row.begin(), row.end(), version_expression);
         iterator != std::sregex_iterator(); ++iterator) {
        version = (*iterator)[1].str();
    }
    if (!version) return std::nullopt;
    const auto slash = url->find_last_of('/');
    if (slash == std::string::npos || slash + 1 == url->size()) return std::nullopt;
    return intel_package(
        "intel-i226-live-" + *release + "-" + *version,
        "Intel Ethernet I226", url->substr(slash + 1), *url, {}, *version, 0,
        intel_size_bytes(text), {"PCI\\VEN_8086&DEV_125C"});
}

OfficialSourceRefresh refresh_official_packages(
    const std::vector<DeviceRecord>& inventory) {
    OfficialSourceRefresh result;
    if (inventory_has_hardware_prefix(inventory, "PCI\\VEN_8086&DEV_125C")) {
        const auto download = fetch_https_text(
            "https://www.intel.com/content/www/us/en/download/15084/"
            "intel-ethernet-adapter-complete-driver-pack.html");
        if (!download.error) {
            const auto text = html_text(download.body);
            const auto release = first_regex_group(
                text, R"(Release_([0-9]+(?:\.[0-9]+)+)\.zip)");
            if (release) {
                auto slug = *release;
                std::replace(slug.begin(), slug.end(), '.', '-');
                const auto notes = fetch_https_text(
                    "https://edc.intel.com/content/www/us/en/design/products/ethernet/"
                    "adapters-and-devices-release-notes/" + *release +
                    "/1-intel-ethernet-controller-products-" + slug + "-release-notes/");
                if (!notes.error) {
                    if (auto package = parse_intel_i226_download_pages(
                               download.body, notes.body)) {
                        result.packages.push_back(std::move(*package));
                    }
                }
            }
        }
    }
    if (inventory_has_hardware_prefix(inventory, "PCI\\VEN_8086&DEV_9A49")) {
        const auto download = fetch_https_text(
            "https://www.intel.com/content/www/us/en/download/864990/"
            "intel-11th-14th-gen-processor-graphics-windows.html");
        if (!download.error) {
            if (auto package = parse_intel_graphics_download_page(download.body)) {
                result.packages.push_back(std::move(*package));
            }
        }
    }
    return result;
}

std::vector<OfficialDriverPackage> official_packages_for_machine(
    const MachineIdentity& identity) {
    const auto manufacturer = lower_ascii(identity.manufacturer);
    const auto combined = lower_ascii(
        identity.product_name + " " + identity.family + " " + identity.sku);
    std::vector<OfficialDriverPackage> result;
    if (manufacturer.find("gmktec") != std::string::npos &&
        (combined.find("m2pro_s") != std::string::npos ||
         combined.find("m2 pro s") != std::string::npos ||
         combined.find("m2pro s") != std::string::npos)) {
        const std::string root =
            "https://pub-34981d9e23754c15a5cbe8af351d046a.r2.dev/"
            "M2%20Pro%20S(Intel%20i7-1185G7)/M2_Pro_S_Driver/";
        result = {
            package("gmktec-m2-pro-s-wifi-20260814", "6.Wifi.zip", root + "6.Wifi.zip",
                    "6001.15.159.0", 65'950'000, {"PCI\\VEN_10EC&DEV_B852"}),
            package("gmktec-m2-pro-s-bt-20260814", "7.BT.zip", root + "7.BT.zip",
                    "18.4021.2412.2701", 84'730'000, {"USB\\VID_0BDA&PID_B85B"}),
            package("gmktec-m2-pro-s-lan-20260814", "8.LAN.zip", root + "8.LAN.zip",
                    "1.1.4.42", 342'760, {"PCI\\VEN_8086&DEV_125C"}),
            package("gmktec-m2-pro-s-other-20260814", "9.Other.zip", root + "9.Other.zip",
                    "2120.100.0.1085", 122'040'000, {"PCI\\VEN_8086&DEV_A0E0"}),
        };
    }
    result.push_back(intel_package(
        "intel-i226-release-31.3-20261019", "Intel Ethernet I226",
        "Release_31.3.zip",
        "https://downloadmirror.intel.com/930885/Release_31.3.zip",
        "", "1.1.4.46",
        20251102, 1'400'000'000,
        {"PCI\\VEN_8086&DEV_125C"}));
    result.push_back(intel_package(
        "intel-graphics-101.7092-20260925", "Intel 11th-14th Gen Graphics",
        "gfx_win_101.7092.exe",
        "https://downloadmirror.intel.com/929468/gfx_win_101.7092.exe",
        "", "32.0.101.7092", 20260925, 744'700'000,
        {"PCI\\VEN_8086&DEV_9A49"}));
    result.push_back(vendor_package(
        "dell-intel-mei-2627.10.5.0", "Dell/Intel (oficial)",
        "Intel Management Engine Interface",
        "Intel-Management-Engine-Components-Installer_FT80J_WIN64_2630.10.15.0_A09_01.EXE",
        "https://dl.dell.com/FOLDER14876649M/2/"
        "Intel-Management-Engine-Components-Installer_FT80J_WIN64_2630.10.15.0_A09_01.EXE",
        "2627.10.5.0", 20260630, 0,
        {"PCI\\VEN_8086&DEV_A0E0"}));
    result.push_back(vendor_package(
        "hp-intel-serial-io-sp165734", "HP/Intel (oficial)",
        "Intel Serial IO", "sp165734.exe",
        "https://ftp.hp.com/pub/softpaq/sp165501-166000/sp165734.exe",
        "5.123.1.1030", 20241115, 0,
        {"PCI\\VEN_8086&DEV_A0E8", "PCI\\VEN_8086&DEV_A0EB",
         "PCI\\VEN_8086&DEV_A0A8", "ACPI\\INT34C5",
         "PCI\\VEN_8086&DEV_A0AB", "PCI\\VEN_8086&DEV_A0EA"}));
    result.push_back(vendor_package(
        "microsoft-realtek-8852be-win10-6001.15.165.3",
        "Microsoft Update Catalog/Realtek (oficial)",
        "Realtek 8852BE Wi-Fi",
        "f69a5370-88eb-4023-886c-3e64260f5876_f512db5869cfdecdc184baa482a687e1c10c5ae8.cab",
        "https://catalog.s.download.windowsupdate.com/d/msdownload/update/driver/drvs/2026/07/"
        "f69a5370-88eb-4023-886c-3e64260f5876_f512db5869cfdecdc184baa482a687e1c10c5ae8.cab",
        "6001.15.165.3", 20260713, 2'477'980,
        {"PCI\\VEN_10EC&DEV_B852&SUBSYS_B85210EC"},
        "NLYFxFFRPxqBLQH8GtEPuS8EYSE/PSEulgyZqg5R+MM=", 19041, 21999));
    return result;
}

bool official_package_matches_device(
    const OfficialDriverPackage& package,
    const DeviceRecord& device) {
    const auto identifiers = device_identifiers(device);
    for (const auto& prefix : package.hardware_id_prefixes) {
        const auto wanted = lower_ascii(prefix);
        if (std::any_of(identifiers.begin(), identifiers.end(), [&](const auto& value) {
                return value.starts_with(wanted);
            })) {
            return true;
        }
    }
    return false;
}

bool official_package_is_newer_than_installed(
    const OfficialDriverPackage& package,
    const DeviceRecord& device) {
    return driver_release_is_newer(
        device.installed_driver.version,
        device.installed_driver.date_yyyymmdd,
        package.driver_version,
        package.published_date_yyyymmdd);
}

bool official_package_supports_windows(
    const OfficialDriverPackage& package,
    std::uint32_t windows_build) {
    if (package.minimum_windows_build != 0 &&
        windows_build < package.minimum_windows_build) {
        return false;
    }
    return package.maximum_windows_build == 0 ||
           windows_build <= package.maximum_windows_build;
}

}  // namespace open_driver

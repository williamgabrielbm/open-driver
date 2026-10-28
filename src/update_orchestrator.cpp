// Language: C++20; file: update_orchestrator.cpp; target: Windows 10/11; runtime: MSVC/CMake.
#include <windows.h>

#include "update_orchestrator.hpp"

#include "candidate_ranker.hpp"
#include "external_package_source.hpp"
#include "official_driver_source.hpp"
#include "package_materializer.hpp"
#include "recommendation_engine.hpp"
#include "update_plan.hpp"
#include "windows_update_source.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <future>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace open_driver {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string display_name(const DeviceRecord& device) {
    if (!device.friendly_name.empty()) return device.friendly_name;
    if (!device.description.empty()) return device.description;
    return device.installed_driver.description;
}

bool is_core_class(const std::string& value) {
    static const std::unordered_set<std::string> classes{
        "display", "net", "media", "hdc", "scsiadapter", "system",
        "bluetooth", "firmware", "hidclass"};
    return classes.contains(lower_ascii(value));
}

bool is_virtual(const DeviceRecord& device) {
    const auto name = lower_ascii(display_name(device));
    const auto id = lower_ascii(device.instance_id);
    return id.starts_with("root\\") || id.starts_with("sw\\") ||
           id.starts_with("swd\\") ||
           name.find("virtual") != std::string::npos ||
           name.find("miniport") != std::string::npos ||
           name.find("tunnel") != std::string::npos ||
           name.find("warp") != std::string::npos ||
           name.find("vpn") != std::string::npos;
}

bool is_microsoft(const std::string& provider) {
    const auto value = lower_ascii(provider);
    return value == "microsoft" || value == "microsoft corporation";
}

std::string generic_hardware_id(const DeviceRecord& device) {
    // Device hardware IDs are the actual physical identity. MatchingDeviceId
    // can be a broad wildcard such as *PNP0C02 shared by unrelated devices.
    std::string value = !device.hardware_ids.empty()
        ? device.hardware_ids.front() : device.installed_driver.matching_device_id;
    const auto upper = upper_ascii(value);
    for (const auto* marker : {"&SUBSYS_", "&REV_", "&CC_"}) {
        const auto position = upper.find(marker);
        if (position != std::string::npos) value.resize(position);
    }
    return value;
}

std::string normalized_hardware_id(std::string value) {
    auto upper = upper_ascii(value);
    for (const auto* marker : {"&REV_", "&CC_"}) {
        const auto position = upper.find(marker);
        if (position != std::string::npos) {
            value.resize(position);
            upper.resize(position);
        }
    }
    return upper_ascii(std::move(value));
}

std::string catalog_query_hardware_id(const DeviceRecord& device) {
    if (!device.hardware_ids.empty()) return normalized_hardware_id(device.hardware_ids.front());
    if (!device.installed_driver.matching_device_id.empty()) {
        return normalized_hardware_id(device.installed_driver.matching_device_id);
    }
    if (!device.compatible_ids.empty()) return normalized_hardware_id(device.compatible_ids.front());
    return {};
}

bool hardware_matches(const DeviceRecord& device, const CatalogDetails& details) {
    std::set<std::string> identifiers;
    for (const auto& value : device.hardware_ids) identifiers.insert(normalized_hardware_id(value));
    for (const auto& value : device.compatible_ids) identifiers.insert(normalized_hardware_id(value));
    if (!device.installed_driver.matching_device_id.empty()) {
        identifiers.insert(normalized_hardware_id(device.installed_driver.matching_device_id));
    }
    for (const auto& candidate : details.hardware_ids) {
        const auto normalized = normalized_hardware_id(candidate);
        if (identifiers.contains(normalized)) return true;
        // A package tied to a different subsystem must never match through the
        // device's broad PCI compatible ID. Generic package IDs remain valid.
        if (normalized.find("&SUBSYS_") != std::string::npos) continue;
        for (const auto& identifier : identifiers) {
            if (identifier.starts_with(normalized)) {
                return true;
            }
        }
    }
    return false;
}

bool architecture_matches(const CatalogDetails& details, const WindowsTarget& target) {
    if (details.architectures.empty()) return true;
    const auto wanted = upper_ascii(target.architecture);
    return std::any_of(details.architectures.begin(), details.architectures.end(),
        [&](const auto& value) {
            const auto architecture = upper_ascii(value);
            return architecture == wanted ||
                   (wanted == "AMD64" && (architecture == "X64" || architecture == "AMD64"));
        });
}

std::uint32_t catalog_date_yyyymmdd(const std::string& value) {
    std::smatch match;
    if (!std::regex_search(value, match, std::regex(R"((\d{1,2})/(\d{1,2})/(\d{4}))"))) {
        return 0;
    }
    const auto month = static_cast<std::uint32_t>(std::stoul(match[1].str()));
    const auto day = static_cast<std::uint32_t>(std::stoul(match[2].str()));
    const auto year = static_cast<std::uint32_t>(std::stoul(match[3].str()));
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    return year * 10000U + month * 100U + day;
}

bool newer_than_installed(
    const DeviceRecord& device,
    const std::string& value,
    std::uint32_t date_yyyymmdd) {
    return driver_release_is_newer(
        device.installed_driver.version,
        device.installed_driver.date_yyyymmdd,
        value,
        date_yyyymmdd);
}

std::vector<CatalogSearchEntry> promising_entries(
    const CatalogSearchResult& search,
    const DeviceRecord& device,
    const WindowsTarget& target) {
    std::vector<CatalogSearchEntry> preferred;
    std::vector<CatalogSearchEntry> fallback;
    std::set<std::pair<std::string, std::uint64_t>> seen;
    for (const auto& entry : search.entries) {
        const auto products = lower_ascii(entry.products);
        if (products.find("server") != std::string::npos) continue;
        const auto version = catalog_entry_version(entry);
        if (!version) continue;
        const auto release_date = catalog_date_yyyymmdd(entry.last_updated);
        // Keep entries whose list page omits a usable date. Their details page
        // carries DriverVer and is the authoritative comparison.
        if (release_date != 0 &&
            !newer_than_installed(device, *version, release_date)) continue;
        if (!seen.emplace(*version, entry.size_bytes).second) continue;
        (catalog_product_matches_windows(entry.products, target) ? preferred : fallback)
            .push_back(entry);
    }
    const auto newest_first = [](const auto& left, const auto& right) {
        const auto left_date = catalog_date_yyyymmdd(left.last_updated);
        const auto right_date = catalog_date_yyyymmdd(right.last_updated);
        if (left_date != right_date) return left_date > right_date;
        const auto a = parse_driver_version(*catalog_entry_version(left));
        const auto b = parse_driver_version(*catalog_entry_version(right));
        return a->parts > b->parts;
    };
    std::sort(preferred.begin(), preferred.end(), newest_first);
    std::sort(fallback.begin(), fallback.end(), newest_first);
    std::vector<CatalogSearchEntry> result;
    result.reserve(preferred.size() + fallback.size());
    result.insert(result.end(), preferred.begin(), preferred.end());
    result.insert(result.end(), fallback.begin(), fallback.end());
    if (result.size() > 8) result.resize(8);
    return result;
}

struct CatalogDeviceLookup {
    std::optional<UpdateOffer> offer;
    std::optional<std::string> error;
};

std::optional<UpdateOffer> inspect_entries(
    const std::vector<CatalogSearchEntry>& entries,
    const DeviceRecord& device,
    const WindowsTarget& target,
    std::vector<std::string>& errors,
    std::stop_token stop);

CatalogDeviceLookup lookup_catalog_update(
    const DeviceRecord& device,
    const WindowsTarget& target,
    std::stop_token stop) {
    if (stop.stop_requested()) return {};
    const auto exact = catalog_query_hardware_id(device);
    if (exact.empty()) return {};
    auto search = search_microsoft_update_catalog(exact);
    if (stop.stop_requested()) return {};
    if (search.error) {
        return {.error = display_name(device) + ": " + search.error->operation +
                         ": " + search.error->message};
    }
    if (search.entries.empty()) {
        const auto generic = generic_hardware_id(device);
        if (!generic.empty() && upper_ascii(generic) != upper_ascii(exact)) {
            search = search_microsoft_update_catalog(generic);
            if (stop.stop_requested()) return {};
            if (search.error) {
                return {.error = display_name(device) + ": " + search.error->operation +
                                 ": " + search.error->message};
            }
        }
    }
    auto entries = promising_entries(search, device, target);
    std::vector<std::string> errors;
    auto offer = inspect_entries(entries, device, target, errors, stop);
    if (offer) {
        offer->source_name = "Microsoft Update Catalog (consulta ao vivo)";
        return {.offer = std::move(offer)};
    }
    if (!errors.empty()) return {.error = std::move(errors.front())};
    return {};
}

std::optional<UpdateOffer> inspect_entries(
    const std::vector<CatalogSearchEntry>& entries,
    const DeviceRecord& device,
    const WindowsTarget& target,
    std::vector<std::string>& errors,
    std::stop_token stop) {
    for (const auto& entry : entries) {
        if (stop.stop_requested()) return std::nullopt;
        const auto response = get_microsoft_update_catalog_details(entry.catalog_update_id);
        if (stop.stop_requested()) return std::nullopt;
        if (response.error || !response.details) {
            errors.push_back(display_name(device) + ": " +
                (response.error ? response.error->operation + ": " + response.error->message
                                : "detalhes ausentes no catálogo"));
            continue;
        }
        const auto& details = *response.details;
        const auto version = !details.version.empty()
            ? std::optional<std::string>(details.version) : catalog_entry_version(entry);
        if (!version || !newer_than_installed(
                device, *version, details.version_date_yyyymmdd) ||
            !hardware_matches(device, details) || !architecture_matches(details, target)) {
            continue;
        }
        // entries arrive newest-first. The first fully compatible package is
        // therefore the winner; older entries cannot improve the decision and
        // do not need extra detail requests.
        return UpdateOffer{
            .device = device,
            .search = entry,
            .details = details,
            .available_version = *version,
            .available_date_yyyymmdd = details.version_date_yyyymmdd,
            .estimated_download_bytes = entry.size_bytes,
        };
    }
    return std::nullopt;
}

std::string package_key(const DeviceRecord& device) {
    return lower_ascii(device.class_name + "|" + device.installed_driver.provider + "|" +
                       device.installed_driver.version + "|" + device.installed_driver.inf_path);
}

std::string timestamp_token() {
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string candidate_version_text(const Candidate& candidate) {
    std::ostringstream output;
    output << candidate.driver_version.parts[0] << '.'
           << candidate.driver_version.parts[1] << '.'
           << candidate.driver_version.parts[2] << '.'
           << candidate.driver_version.parts[3];
    return output.str();
}

}  // namespace

UpdateAttemptPaths update_attempt_paths(
    const std::filesystem::path& data_root,
    const std::string& plan_id,
    const std::string& attempt_token) {
    const auto attempt_name = plan_id + "-" + attempt_token;
    return {
        .plan = data_root / L"plans" / std::filesystem::path(attempt_name + ".json"),
        .journal = data_root / L"journals" / std::filesystem::path(attempt_name + ".jsonl"),
    };
}

bool device_snapshot_matches_offer(
    const UpdateOffer& offer,
    const DeviceRecord& current_device) {
    const auto& expected = offer.device.installed_driver;
    const auto& observed = current_device.installed_driver;
    return current_device.present &&
           lower_ascii(offer.device.instance_id) == lower_ascii(current_device.instance_id) &&
           lower_ascii(expected.provider) == lower_ascii(observed.provider) &&
           expected.version == observed.version &&
           expected.date_yyyymmdd == observed.date_yyyymmdd &&
           lower_ascii(std::filesystem::path(expected.inf_path).filename().string()) ==
               lower_ascii(std::filesystem::path(observed.inf_path).filename().string());
}

bool driver_component_is_scannable(const DeviceRecord& device) {
    if (!device.present || !parse_driver_version(device.installed_driver.version) ||
        device.installed_driver.inf_path.empty() || is_virtual(device)) {
        return false;
    }
    const auto instance = lower_ascii(device.instance_id);
    if (instance.starts_with("htree\\") || instance.starts_with("umb\\")) return false;
    static const std::unordered_set<std::string> software_only_classes{
        "softwaredevice", "audioendpoint", "processor",
        "volume", "volumesnapshot", "computer", "printqueue"};
    if (software_only_classes.contains(lower_ascii(device.class_name))) return false;
    return !device.hardware_ids.empty() || !device.compatible_ids.empty() ||
           !device.installed_driver.matching_device_id.empty();
}

bool catalog_cross_check_is_useful(const DeviceRecord& device) {
    if (lower_ascii(device.installed_driver.provider) == "microsoft") return false;
    const auto identity = upper_ascii(generic_hardware_id(device));
    static constexpr std::array prefixes{
        "PCI\\", "USB\\VID_", "HDAUDIO\\", "ACPI\\", "SCSI\\",
        "STORAGE\\", "MONITOR\\", "UEFI\\", "SD\\", "HID\\VID_"};
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const char* prefix) {
        return identity.starts_with(prefix);
    });
}

WindowsTarget current_windows_target() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (const auto module = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto function = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(module, "RtlGetVersion"))) {
            function(&version);
        }
    }
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    std::string architecture = "UNKNOWN";
    if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) architecture = "AMD64";
    else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) architecture = "ARM64";
    else if (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) architecture = "X86";
    return {.build = version.dwBuildNumber,
            .windows_11 = version.dwBuildNumber >= 22000,
            .architecture = std::move(architecture)};
}

bool catalog_product_matches_windows(
    const std::string& products,
    const WindowsTarget& target) {
    const auto value = lower_ascii(products);
    if (value.find("server") != std::string::npos) return false;
    if (target.windows_11) {
        return value.find("windows 11") != std::string::npos ||
               value.find("windows - client") != std::string::npos;
    }
    if (value.find("windows 11") != std::string::npos) return false;
    return value.find("windows 10") != std::string::npos ||
           value.find("windows - client") != std::string::npos;
}

std::optional<std::string> catalog_entry_version(const CatalogSearchEntry& entry) {
    if (parse_driver_version(entry.version)) return entry.version;
    static const std::regex expression(R"((\d+\.\d+\.\d+\.\d+))");
    std::optional<std::string> result;
    for (auto iterator = std::sregex_iterator(entry.title.begin(), entry.title.end(), expression);
         iterator != std::sregex_iterator(); ++iterator) {
        result = (*iterator)[1].str();
    }
    return result;
}

UpdateDiscoveryReport discover_updates(
    const std::vector<DeviceRecord>& inventory,
    const DiscoveryProgress& progress,
    std::stop_token stop) {
    UpdateDiscoveryReport report;
    report.target = current_windows_target();
    report.devices_detected = inventory.size();
    struct DeviceGroup {
        std::string key;
        DeviceRecord representative;
        std::vector<DeviceRecord> members;
    };
    std::vector<DeviceGroup> devices;
    std::unordered_set<std::string> seen;
    for (const auto& device : inventory) {
        if (stop.stop_requested()) {
            report.cancelled = true;
            return report;
        }
        if (!driver_component_is_scannable(device)) continue;
        // Every hardware identity gets its own catalog lookup. One installed INF
        // can cover unrelated chips; using one representative loses updates for
        // the other devices in that package.
        auto identity = upper_ascii(generic_hardware_id(device));
        if (identity.empty()) identity = upper_ascii(device.instance_id);
        auto key = package_key(device) + "|" + identity;
        const auto existing = std::find_if(devices.begin(), devices.end(), [&](const auto& item) {
            return item.key == key;
        });
        if (existing != devices.end()) {
            existing->members.push_back(device);
            continue;
        }
        if (seen.insert(key).second) devices.push_back({key, device, {device}});
    }
    report.packages_checked = devices.size();
    for (const auto& group : devices) report.devices_checked += group.members.size();
    std::vector<DeviceGroup> catalog_devices;
    std::copy_if(devices.begin(), devices.end(), std::back_inserter(catalog_devices),
        [](const auto& group) { return catalog_cross_check_is_useful(group.representative); });
    // Every identity is inventoried locally. One Windows Update Agent search
    // then evaluates applicability for the complete machine in a single
    // supported operation; the public Catalog website is not a bulk-scan API.
    // The Windows Update Agent is the supported whole-machine applicability
    // scan. Public Catalog lookups below only enrich that result and must not
    // turn a complete scan into a failure when the website throttles scraping.
    report.source_checks_required = 1;
    WindowsUpdateSearchResult windows_update;
    constexpr std::array retry_delays{std::chrono::milliseconds(250),
                                      std::chrono::milliseconds(750)};
    for (std::size_t attempt = 0; attempt <= retry_delays.size(); ++attempt) {
        if (stop.stop_requested()) {
            report.cancelled = true;
            return report;
        }
        windows_update = search_windows_update_drivers(inventory, true, stop);
        if (windows_update.cancelled) {
            report.cancelled = true;
            return report;
        }
        if (stop.stop_requested()) {
            report.cancelled = true;
            return report;
        }
        if (!windows_update.fatal_error || attempt == retry_delays.size()) break;
        const auto until = std::chrono::steady_clock::now() + retry_delays[attempt];
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    if (windows_update.fatal_error) {
        report.errors.push_back(
            "Windows Update: " + windows_update.fatal_error->message +
            "; falhou após 3 tentativas automáticas");
    } else {
        ++report.source_checks_completed;
        report.windows_update_candidates = windows_update.updates.size();
        for (const auto& update : windows_update.updates) {
            report.windows_update_present_candidates +=
                !update.matching_present_instance_ids.empty();
            for (const auto& instance_id : update.matching_present_instance_ids) {
                const auto device = std::find_if(inventory.begin(), inventory.end(),
                    [&](const auto& value) {
                        return upper_ascii(value.instance_id) == upper_ascii(instance_id);
                    });
                if (device == inventory.end() || !driver_component_is_scannable(*device)) continue;
                const bool duplicate = std::any_of(
                    report.offers.begin(), report.offers.end(), [&](const auto& offer) {
                        return upper_ascii(offer.device.instance_id) == upper_ascii(instance_id) &&
                               offer.search.catalog_update_id == update.update_id;
                    });
                if (duplicate) continue;
                UpdateOffer offer;
                offer.device = *device;
                offer.search.catalog_update_id = update.update_id;
                offer.search.title = update.title;
                offer.source_name = "Windows Update (oficial)";
                offer.available_date_yyyymmdd = update.driver_date_yyyymmdd;
                report.offers.push_back(std::move(offer));
            }
        }
    }

    // Windows Update evaluates the whole machine above. Cross-check the public
    // Catalog only for physical bus identities; profile, endpoint, printer and
    // other virtual identities use inbox drivers and can make the website
    // return an error page instead of a valid empty result.
    constexpr std::size_t catalog_parallelism = 3;
    for (std::size_t start = 0; start < catalog_devices.size(); start += catalog_parallelism) {
        if (stop.stop_requested()) {
            report.cancelled = true;
            report.offers.clear();
            return report;
        }
        const auto end = std::min(catalog_devices.size(), start + catalog_parallelism);
        const auto errors_before_batch = report.errors.size();
        std::size_t failed_lookups = 0;
        std::vector<std::size_t> failed_indices;
        std::vector<std::future<CatalogDeviceLookup>> lookups;
        lookups.reserve(end - start);
        for (std::size_t index = start; index < end; ++index) {
            if (progress) {
                progress(index + 1, catalog_devices.size(),
                         display_name(catalog_devices[index].representative));
            }
            const auto device = catalog_devices[index].representative;
            const auto target = report.target;
            lookups.push_back(std::async(std::launch::async, [device, target, stop] {
                return lookup_catalog_update(device, target, stop);
            }));
        }
        for (std::size_t offset = 0; offset < lookups.size(); ++offset) {
            try {
                auto lookup = lookups[offset].get();
                if (stop.stop_requested()) {
                    report.cancelled = true;
                    report.offers.clear();
                    return report;
                }
                if (lookup.error) {
                    ++failed_lookups;
                    failed_indices.push_back(start + offset);
                    report.errors.push_back(*lookup.error);
                    continue;
                }
                if (lookup.offer) {
                    lookup.offer->related_devices = catalog_devices[start + offset].members.size();
                    report.offers.push_back(std::move(*lookup.offer));
                }
            } catch (const std::exception& error) {
                ++failed_lookups;
                failed_indices.push_back(start + offset);
                report.errors.push_back(
                    display_name(catalog_devices[start + offset].representative) +
                    ": falha interna na consulta ao Catálogo: " + error.what());
            } catch (...) {
                ++failed_lookups;
                failed_indices.push_back(start + offset);
                report.errors.push_back(
                    display_name(catalog_devices[start + offset].representative) +
                    ": falha interna desconhecida na consulta ao Catálogo");
            }
        }
        if (failed_lookups != 0) {
            // A failed lookup can be a short Catalog throttle or a transient
            // TLS failure. Cool down and retry failed identities without
            // parallel connections before marking the scan incomplete.
            report.errors.resize(errors_before_batch);
            const auto delay = failed_lookups == lookups.size()
                ? std::chrono::milliseconds(1500)
                : std::chrono::milliseconds(500);
            const auto until = std::chrono::steady_clock::now() + delay;
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            std::size_t retry_failures = 0;
            for (const auto index : failed_indices) {
                if (stop.stop_requested()) {
                    report.cancelled = true;
                    report.offers.clear();
                    return report;
                }
                auto retry = lookup_catalog_update(
                    catalog_devices[index].representative, report.target, stop);
                if (retry.error) {
                    ++retry_failures;
                    continue;
                }
                if (retry.offer) {
                    retry.offer->related_devices = catalog_devices[index].members.size();
                    report.offers.push_back(std::move(*retry.offer));
                }
            }
            if (failed_lookups == lookups.size() &&
                retry_failures == failed_indices.size()) {
                break;
            }
        }
    }
    if (stop.stop_requested()) {
        report.cancelled = true;
        report.offers.clear();
        return report;
    }
    auto refreshed_official = refresh_official_packages(inventory);
    if (stop.stop_requested()) {
        report.cancelled = true;
        report.offers.clear();
        return report;
    }
    report.source_checks_required += refreshed_official.checks_required;
    report.source_checks_completed += refreshed_official.checks_completed;
    report.errors.insert(report.errors.end(),
                         refreshed_official.errors.begin(), refreshed_official.errors.end());
    auto official_packages = std::move(refreshed_official.packages);
    auto bundled_official = official_packages_for_machine(current_machine_identity());
    official_packages.insert(official_packages.end(),
                             std::make_move_iterator(bundled_official.begin()),
                             std::make_move_iterator(bundled_official.end()));
    for (const auto& package : official_packages) {
        if (stop.stop_requested()) {
            report.cancelled = true;
            report.offers.clear();
            return report;
        }
        if (!official_package_supports_windows(package, report.target.build)) continue;
        for (const auto& group : devices) {
            if (!official_package_matches_device(package, group.representative) ||
                !official_package_is_newer_than_installed(package, group.representative)) {
                continue;
            }
            UpdateOffer offer;
            offer.device = group.representative;
            offer.source_name = package.source_name;
            offer.source_package_id = package.id;
            offer.source_files = {package.file};
            offer.source_hash_is_published = !package.file.sha256_base64.empty();
            offer.available_version = package.driver_version;
            offer.available_date_yyyymmdd = package.published_date_yyyymmdd;
            offer.estimated_download_bytes = package.size_bytes;
            offer.related_devices = group.members.size();
            report.offers.push_back(std::move(offer));
        }
    }

    std::vector<UpdateOffer> unique_offers;
    unique_offers.reserve(report.offers.size());
    for (auto& offer : report.offers) {
        const auto existing = std::find_if(unique_offers.begin(), unique_offers.end(),
            [&](const auto& item) {
                return upper_ascii(item.device.instance_id) ==
                       upper_ascii(offer.device.instance_id);
            });
        if (existing == unique_offers.end()) {
            unique_offers.push_back(std::move(offer));
            continue;
        }
        const bool incoming_is_newer = driver_release_is_newer(
            existing->available_version,
            existing->available_date_yyyymmdd,
            offer.available_version,
            offer.available_date_yyyymmdd);
        const bool existing_is_live = existing->source_name.find("consulta ao vivo") !=
                                      std::string::npos;
        const bool incoming_is_live = offer.source_name.find("consulta ao vivo") !=
                                      std::string::npos;
        const bool stale_fallback_cannot_replace_live =
            existing_is_live && !incoming_is_live && !incoming_is_newer;
        if (!stale_fallback_cannot_replace_live && incoming_is_newer) {
            *existing = std::move(offer);
        }
    }
    report.offers = std::move(unique_offers);
    std::sort(report.offers.begin(), report.offers.end(), [](const auto& left, const auto& right) {
        return display_name(left.device) < display_name(right.device);
    });
    report.complete = report.errors.empty() &&
                      report.source_checks_completed == report.source_checks_required;
    if (!report.complete) report.offers.clear();
    return report;
}

PrepareUpdateResult prepare_update(
    const UpdateOffer& offer,
    const std::filesystem::path& application_data_root,
    std::stop_token stop) {
    PrepareUpdateResult result;
    try {
    if (stop.stop_requested()) {
        result.error = "operação cancelada";
        return result;
    }
    std::error_code filesystem_error;
    const auto data_root = std::filesystem::absolute(application_data_root, filesystem_error);
    if (filesystem_error) {
        result.error = "não foi possível resolver a pasta de trabalho: " + filesystem_error.message();
        return result;
    }
    std::filesystem::create_directories(data_root / L"packages", filesystem_error);
    std::filesystem::create_directories(data_root / L"plans", filesystem_error);
    std::filesystem::create_directories(data_root / L"journals", filesystem_error);
    std::filesystem::create_directories(data_root / L"rollbacks", filesystem_error);
    if (filesystem_error) {
        result.error = "não foi possível criar a pasta de trabalho: " + filesystem_error.message();
        return result;
    }

    std::vector<CatalogFile> package_files;
    std::optional<std::filesystem::path> source_directory;
    const auto package_id = !offer.source_package_id.empty()
        ? offer.source_package_id : offer.search.catalog_update_id;
    if (!offer.source_files.empty()) {
        source_directory = data_root / L"source-cache" / std::filesystem::path(package_id);
        std::filesystem::create_directories(*source_directory, filesystem_error);
        if (filesystem_error) {
            result.error = "não foi possível criar o cache da fonte oficial: " +
                           filesystem_error.message();
            return result;
        }
        for (auto file : offer.source_files) {
            if (stop.stop_requested()) {
                result.error = "operação cancelada";
                return result;
            }
            const auto name = std::filesystem::path(file.file_name).filename();
            if (name.empty() || name != std::filesystem::path(file.file_name)) {
                result.error = "a fonte oficial informou um nome de arquivo inválido";
                return result;
            }
            const auto cached = *source_directory / name;
            filesystem_error.clear();
            const bool cached_exists = std::filesystem::exists(cached, filesystem_error);
            if (filesystem_error) {
                result.error = "não foi possível consultar o cache oficial: " +
                               filesystem_error.message();
                return result;
            }
            if (!cached_exists) {
                auto download = download_catalog_file(file, cached, true);
                if (download.error) {
                    result.error = "download da fonte oficial: " + download.error->message;
                    return result;
                }
                file.sha256_base64 = download.sha256_base64;
            } else {
                file.sha256_base64 = sha256_file_base64(cached);
                if (file.sha256_base64.empty()) {
                    result.error = "não foi possível verificar o arquivo no cache oficial";
                    return result;
                }
            }
            package_files.push_back(std::move(file));
        }
    } else {
        if (stop.stop_requested()) {
            result.error = "operação cancelada";
            return result;
        }
        const auto files = get_microsoft_update_catalog_files(offer.search.catalog_update_id);
        if (files.error || files.files.empty()) {
            result.error = files.error ? files.error->message : "o catálogo não retornou o arquivo";
            return result;
        }
        package_files = files.files;
    }
    const auto token = timestamp_token();
    const auto package_directory = data_root / L"packages" /
        std::filesystem::path(package_id + "-" + token);
    const auto materialized = materialize_catalog_package(
        package_files, package_directory, source_directory);
    if (stop.stop_requested()) {
        result.error = "operação cancelada";
        return result;
    }
    if (materialized.error) {
        result.error = materialized.error->operation + ": " + materialized.error->message;
        return result;
    }

    std::vector<Candidate> candidates;
    for (const auto& inf : materialized.inf_files) {
        if (!inf.referenced_catalogs_present) continue;
        auto analysis = analyze_external_inf_for_device(offer.device, path_utf8(inf.path));
        candidates.insert(candidates.end(),
                          std::make_move_iterator(analysis.candidates.begin()),
                          std::make_move_iterator(analysis.candidates.end()));
    }
    auto ranking = rank_candidates(std::move(candidates));
    if (ranking.accepted.empty()) {
        result.rejected = true;
        result.error = "o pacote baixado não serve para este dispositivo";
        return result;
    }
    const auto candidate = ranking.accepted.front().candidate;
    const auto valid_catalog = std::find_if(materialized.catalogs.begin(), materialized.catalogs.end(),
        [](const auto& catalog) { return catalog.valid; });
    const auto archive = std::find_if(materialized.downloads.begin(), materialized.downloads.end(),
        [](const auto& download) { return download.hash_matches; });
    if (valid_catalog == materialized.catalogs.end() || archive == materialized.downloads.end()) {
        result.rejected = true;
        result.error = "o pacote não passou na verificação de assinatura e integridade";
        return result;
    }
    const auto recommendation = recommend_driver_action(
        offer.device, candidate,
        {.catalog_resolution_unambiguous = true,
         .package_hash_verified = true,
         .catalog_signature_valid = true});
    if (recommendation.decision != RecommendationDecision::update) {
        result.rejected = true;
        result.error = "a validação final concluiu que este driver não deve substituir o atual";
        return result;
    }
    const PackageEvidence evidence{
        .catalog_update_id = package_id,
        .cab_path = path_utf8(archive->path),
        .cab_sha256_base64 = archive->sha256_base64,
        .catalog_path = path_utf8(valid_catalog->path),
        .inf_path = candidate.inf_path,
        .inf_sha256_hex = candidate.package_hash,
        .cab_hash_verified = archive->hash_matches,
        .catalog_signature_valid = true,
    };
    const auto rollback = data_root / L"rollbacks" /
        std::filesystem::path(package_id + "-" + token);
    const auto plan = build_update_plan(
        offer.device, candidate, recommendation, evidence,
        {.rollback_directory = path_utf8(rollback), .create_restore_point = true});
    if (!plan.ready) {
        result.rejected = true;
        result.error = plan.blockers.empty() ? "o plano de instalação não ficou pronto"
                                             : plan.blockers.front();
        return result;
    }
    // plan_id identifies the selected device and package. token identifies this
    // execution attempt, so retries never overwrite an earlier plan or journal.
    const auto attempt_paths = update_attempt_paths(data_root, plan.plan_id, token);
    const auto& plan_path = attempt_paths.plan;
    std::ofstream output(plan_path, std::ios::binary | std::ios::out);
    output << update_plan_json(plan);
    if (!output) {
        result.error = "não foi possível gravar o plano de instalação";
        return result;
    }
    result.prepared = PreparedUpdate{
        .package_directory = package_directory,
        .plan_path = plan_path,
        .journal_path = attempt_paths.journal,
        .plan_id = plan.plan_id,
    };
    result.available_version = candidate_version_text(candidate);
    result.available_date_yyyymmdd = candidate.driver_date;
    return result;
    } catch (const std::exception& error) {
        result.error = std::string("falha interna ao preparar o pacote: ") + error.what();
        return result;
    } catch (...) {
        result.error = "falha interna desconhecida ao preparar o pacote";
        return result;
    }
}

void validate_and_prepare_offers(
    UpdateDiscoveryReport& report,
    const std::filesystem::path& application_data_root,
    const DiscoveryProgress& progress,
    std::stop_token stop) {
    if (!report.complete) {
        report.offers.clear();
        return;
    }
    std::vector<UpdateOffer> validated;
    validated.reserve(report.offers.size());
    const auto total = report.offers.size();
    for (std::size_t index = 0; index < total; ++index) {
        if (stop.stop_requested()) {
            report.cancelled = true;
            report.complete = false;
            report.offers.clear();
            return;
        }
        auto& offer = report.offers[index];
        if (progress) progress(index + 1, total, display_name(offer.device));
        auto prepared = prepare_update(offer, application_data_root, stop);
        if (stop.stop_requested()) {
            report.cancelled = true;
            report.complete = false;
            report.offers.clear();
            return;
        }
        ++report.candidates_validated;
        if (!prepared.prepared) {
            const auto message = display_name(offer.device) + ": " + prepared.error;
            if (prepared.rejected) {
                ++report.candidates_rejected;
                report.rejections.push_back(message);
            } else {
                report.errors.push_back(message);
            }
            continue;
        }
        offer.prepared = std::move(prepared.prepared);
        offer.available_version = prepared.available_version;
        offer.available_date_yyyymmdd = prepared.available_date_yyyymmdd;
        const auto duplicate = std::find_if(validated.begin(), validated.end(), [&](const auto& item) {
            return lower_ascii(item.device.instance_id) == lower_ascii(offer.device.instance_id);
        });
        if (duplicate == validated.end()) {
            validated.push_back(std::move(offer));
        } else {
            const auto current = parse_driver_version(duplicate->available_version);
            const auto incoming = parse_driver_version(offer.available_version);
            const bool incoming_is_newer = driver_release_is_newer(
                duplicate->available_version,
                duplicate->available_date_yyyymmdd,
                offer.available_version,
                offer.available_date_yyyymmdd);
            const bool same_but_official = incoming && current && incoming->parts == current->parts &&
                offer.source_name.find("oficial") != std::string::npos;
            if (incoming_is_newer || same_but_official) {
                *duplicate = std::move(offer);
            }
        }
    }
    report.offers = std::move(validated);
    report.complete = report.errors.empty() &&
                      report.source_checks_completed == report.source_checks_required;
    if (!report.complete) report.offers.clear();
}

}  // namespace open_driver

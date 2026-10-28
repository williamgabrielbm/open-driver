// Language: C++20; file: driver_health.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "driver_health.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace open_driver {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool contains_any(const std::string& value, const std::vector<std::string>& needles) {
    const auto lower = lower_ascii(value);
    return std::any_of(needles.begin(), needles.end(), [&](const auto& needle) {
        return lower.find(needle) != std::string::npos;
    });
}

bool is_core_class(const std::string& class_name) {
    static const std::unordered_set<std::string> classes{
        "display", "net", "media", "hdc", "scsiadapter", "system",
        "bluetooth", "firmware"};
    return classes.contains(lower_ascii(class_name));
}

bool is_microsoft_provider(const std::string& provider) {
    const auto lower = lower_ascii(provider);
    return lower == "microsoft" || lower == "microsoft corporation";
}

bool is_virtual_device(const DeviceRecord& device) {
    const auto name = device.friendly_name.empty() ? device.description : device.friendly_name;
    if (contains_any(name, {"virtual", "miniport", "tunnel", "warp", "vpn", "hyper-v"})) {
        return true;
    }
    const auto id = lower_ascii(device.instance_id);
    return id.starts_with("root\\") || id.starts_with("sw\\") ||
           id.find("wintun") != std::string::npos;
}

std::uint32_t one_year_before(std::uint32_t date) {
    const auto year = date / 10000U;
    return year > 0 ? (year - 1U) * 10000U + date % 10000U : 0U;
}

std::string display_name(const DeviceRecord& device) {
    if (!device.friendly_name.empty()) return device.friendly_name;
    if (!device.description.empty()) return device.description;
    return device.installed_driver.description;
}

bool same_package(const DriverHealthItem& item, const DeviceRecord& device) {
    return lower_ascii(item.class_name) == lower_ascii(device.class_name) &&
           lower_ascii(item.provider) == lower_ascii(device.installed_driver.provider) &&
           item.version == device.installed_driver.version &&
           lower_ascii(item.inf_path) == lower_ascii(device.installed_driver.inf_path) &&
           item.driver_date_yyyymmdd == device.installed_driver.date_yyyymmdd;
}

}  // namespace

DriverHealthReport assess_driver_health(
    const std::vector<DeviceRecord>& inventory,
    const std::vector<WindowsUpdateDriverRecord>& updates,
    std::uint32_t today_yyyymmdd) {
    DriverHealthReport report;
    std::unordered_set<std::string> confirmed_instances;
    for (const auto& update : updates) {
        for (const auto& instance_id : update.matching_present_instance_ids) {
            confirmed_instances.insert(lower_ascii(instance_id));
        }
    }

    const auto old_driver_cutoff = one_year_before(today_yyyymmdd);
    for (const auto& device : inventory) {
        if (!device.present) continue;
        ++report.present_devices;
        if (!is_core_class(device.class_name)) continue;

        const bool confirmed = confirmed_instances.contains(lower_ascii(device.instance_id));
        const bool attention = device.problem_code != 0;
        if (!confirmed && !attention) {
            if (is_virtual_device(device)) {
                ++report.ignored_virtual_devices;
                continue;
            }
            if (is_microsoft_provider(device.installed_driver.provider)) {
                ++report.ignored_windows_drivers;
                continue;
            }
            if (device.installed_driver.date_yyyymmdd != 0 &&
                device.installed_driver.date_yyyymmdd < 20100101U) {
                ++report.ignored_fixed_dates;
                continue;
            }
            if (device.installed_driver.date_yyyymmdd == 0 ||
                device.installed_driver.date_yyyymmdd >= old_driver_cutoff) {
                continue;
            }
        }

        DriverHealthStatus status = DriverHealthStatus::needs_oem_check;
        std::string explanation =
            "O driver foi publicado ha mais de um ano. O Windows Update nao ofereceu outro; "
            "compare com o pacote oficial do fabricante.";
        if (confirmed) {
            status = DriverHealthStatus::confirmed_update;
            explanation =
                "O Windows Update encontrou um pacote para este dispositivo conectado.";
            ++report.confirmed_updates;
        } else if (attention) {
            status = DriverHealthStatus::device_attention;
            explanation =
                "O Windows marcou este dispositivo com um problema que precisa de verificacao.";
            ++report.device_attention;
        } else {
            ++report.needs_oem_check;
        }

        auto existing = std::find_if(report.items.begin(), report.items.end(),
            [&](const auto& item) {
                return item.status == status && same_package(item, device);
            });
        if (existing != report.items.end()) {
            ++existing->related_devices;
            continue;
        }

        report.items.push_back({
            .device_name = display_name(device),
            .class_name = device.class_name,
            .provider = device.installed_driver.provider,
            .version = device.installed_driver.version,
            .inf_path = device.installed_driver.inf_path,
            .driver_date_yyyymmdd = device.installed_driver.date_yyyymmdd,
            .status = status,
            .plain_explanation = std::move(explanation),
        });
    }

    std::sort(report.items.begin(), report.items.end(), [](const auto& left, const auto& right) {
        if (left.status != right.status) return left.status < right.status;
        if (left.driver_date_yyyymmdd != right.driver_date_yyyymmdd) {
            return left.driver_date_yyyymmdd < right.driver_date_yyyymmdd;
        }
        return left.device_name < right.device_name;
    });
    return report;
}

std::uint32_t local_date_yyyymmdd() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return static_cast<std::uint32_t>(time.wYear) * 10000U +
           static_cast<std::uint32_t>(time.wMonth) * 100U +
           static_cast<std::uint32_t>(time.wDay);
}

}  // namespace open_driver

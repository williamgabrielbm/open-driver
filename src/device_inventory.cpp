// Language: C++20; file: device_inventory.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <objbase.h>
#include <setupapi.h>

#include "device_inventory.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

namespace open_driver {
namespace {

class DeviceInfoSet final {
public:
    explicit DeviceInfoSet(HDEVINFO handle) : handle_(handle) {}
    ~DeviceInfoSet() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            SetupDiDestroyDeviceInfoList(handle_);
        }
    }

    DeviceInfoSet(const DeviceInfoSet&) = delete;
    DeviceInfoSet& operator=(const DeviceInfoSet&) = delete;

    [[nodiscard]] HDEVINFO get() const { return handle_; }

private:
    HDEVINFO handle_{INVALID_HANDLE_VALUE};
};

struct PropertyBytes {
    DEVPROPTYPE type{};
    std::vector<std::byte> bytes;
};

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, nullptr);
    return result;
}

std::string windows_error_message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageW(
        flags, nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (size == 0 || buffer == nullptr) {
        return "Windows error " + std::to_string(error);
    }

    std::wstring message(buffer, size);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return utf8_from_wide(message);
}

void record_error(
    std::vector<InventoryError>& errors,
    std::string operation,
    const std::string& instance_id,
    DWORD error) {
    errors.push_back({
        .operation = std::move(operation),
        .device_instance_id = instance_id,
        .windows_error = error,
        .message = windows_error_message(error),
    });
}

std::optional<PropertyBytes> read_property(
    HDEVINFO device_info_set,
    SP_DEVINFO_DATA& device,
    const DEVPROPKEY& key,
    const std::string& property_name,
    const std::string& instance_id,
    std::vector<InventoryError>& errors) {
    DEVPROPTYPE type = 0;
    DWORD required = 0;
    if (!SetupDiGetDevicePropertyW(
            device_info_set, &device, &key, &type, nullptr, 0, &required, 0)) {
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_FOUND || error == ERROR_INVALID_DATA) {
            return std::nullopt;
        }
        if (error != ERROR_INSUFFICIENT_BUFFER) {
            record_error(errors, property_name, instance_id, error);
            return std::nullopt;
        }
    }

    if (required == 0) {
        return PropertyBytes{.type = type};
    }

    PropertyBytes result{.type = type, .bytes = std::vector<std::byte>(required)};
    if (!SetupDiGetDevicePropertyW(
            device_info_set,
            &device,
            &key,
            &result.type,
            reinterpret_cast<PBYTE>(result.bytes.data()),
            required,
            &required,
            0)) {
        record_error(errors, property_name, instance_id, GetLastError());
        return std::nullopt;
    }
    return result;
}

std::string property_string(const std::optional<PropertyBytes>& property) {
    if (!property || property->type != DEVPROP_TYPE_STRING || property->bytes.empty()) {
        return {};
    }
    const auto* value = reinterpret_cast<const wchar_t*>(property->bytes.data());
    const std::size_t count = property->bytes.size() / sizeof(wchar_t);
    const auto end = std::find(value, value + count, L'\0');
    return utf8_from_wide(std::wstring(value, end));
}

std::vector<std::string> property_string_list(
    const std::optional<PropertyBytes>& property) {
    std::vector<std::string> values;
    if (!property || property->type != DEVPROP_TYPE_STRING_LIST || property->bytes.empty()) {
        return values;
    }

    const auto* cursor = reinterpret_cast<const wchar_t*>(property->bytes.data());
    const auto* end = cursor + property->bytes.size() / sizeof(wchar_t);
    while (cursor < end && *cursor != L'\0') {
        const auto* terminator = std::find(cursor, end, L'\0');
        values.push_back(utf8_from_wide(std::wstring(cursor, terminator)));
        if (terminator == end) {
            break;
        }
        cursor = terminator + 1;
    }
    return values;
}

std::uint32_t property_date(const std::optional<PropertyBytes>& property) {
    if (!property || property->type != DEVPROP_TYPE_FILETIME ||
        property->bytes.size() < sizeof(FILETIME)) {
        return 0;
    }

    FILETIME file_time{};
    std::memcpy(&file_time, property->bytes.data(), sizeof(file_time));
    SYSTEMTIME system_time{};
    if (!FileTimeToSystemTime(&file_time, &system_time)) {
        return 0;
    }
    return static_cast<std::uint32_t>(system_time.wYear) * 10000U +
           static_cast<std::uint32_t>(system_time.wMonth) * 100U +
           static_cast<std::uint32_t>(system_time.wDay);
}

bool property_bool(const std::optional<PropertyBytes>& property, bool fallback) {
    if (!property || property->type != DEVPROP_TYPE_BOOLEAN || property->bytes.empty()) {
        return fallback;
    }
    return *reinterpret_cast<const DEVPROP_BOOLEAN*>(property->bytes.data()) != DEVPROP_FALSE;
}

std::string guid_string(const GUID& guid) {
    wchar_t buffer[39]{};
    const int count = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    return count > 0 ? utf8_from_wide(buffer) : std::string{};
}

std::string get_instance_id(
    HDEVINFO device_info_set,
    SP_DEVINFO_DATA& device,
    std::vector<InventoryError>& errors) {
    DWORD required = 0;
    SetupDiGetDeviceInstanceIdW(device_info_set, &device, nullptr, 0, &required);
    const DWORD first_error = GetLastError();
    if (first_error != ERROR_INSUFFICIENT_BUFFER || required == 0) {
        record_error(errors, "SetupDiGetDeviceInstanceIdW", {}, first_error);
        return {};
    }

    std::wstring value(required, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(
            device_info_set, &device, value.data(), required, &required)) {
        record_error(errors, "SetupDiGetDeviceInstanceIdW", {}, GetLastError());
        return {};
    }
    value.resize(std::char_traits<wchar_t>::length(value.c_str()));
    return utf8_from_wide(value);
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec;
                } else {
                    output << character;
                }
        }
    }
    return output.str();
}

void write_string(std::ostream& output, const std::string& value) {
    output << '"' << json_escape(value) << '"';
}

void write_string_array(std::ostream& output, const std::vector<std::string>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_string(output, values[index]);
    }
    output << ']';
}

}  // namespace

DeviceInventoryResult collect_device_inventory(const DeviceInventoryOptions& options) {
    DeviceInventoryResult result;
    const DWORD flags = DIGCF_ALLCLASSES | (options.present_only ? DIGCF_PRESENT : 0U);
    DeviceInfoSet device_info_set(SetupDiGetClassDevsW(nullptr, nullptr, nullptr, flags));
    if (device_info_set.get() == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        result.fatal_error = InventoryError{
            .operation = "SetupDiGetClassDevsW",
            .windows_error = error,
            .message = windows_error_message(error),
        };
        return result;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(device_info_set.get(), index, &device)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                result.fatal_error = InventoryError{
                    .operation = "SetupDiEnumDeviceInfo",
                    .windows_error = error,
                    .message = windows_error_message(error),
                };
            }
            break;
        }

        DeviceRecord record;
        record.instance_id = get_instance_id(
            device_info_set.get(), device, result.property_errors);
        const auto read = [&](const DEVPROPKEY& key, const char* name) {
            return read_property(
                device_info_set.get(), device, key, name, record.instance_id,
                result.property_errors);
        };

        record.class_name = property_string(read(DEVPKEY_Device_Class, "Device.Class"));
        record.class_guid = guid_string(device.ClassGuid);
        record.description = property_string(read(DEVPKEY_Device_DeviceDesc, "Device.Description"));
        record.friendly_name = property_string(read(DEVPKEY_Device_FriendlyName, "Device.FriendlyName"));
        record.manufacturer = property_string(read(DEVPKEY_Device_Manufacturer, "Device.Manufacturer"));
        record.location = property_string(read(DEVPKEY_Device_LocationInfo, "Device.LocationInfo"));
        record.hardware_ids = property_string_list(read(DEVPKEY_Device_HardwareIds, "Device.HardwareIds"));
        record.compatible_ids = property_string_list(read(DEVPKEY_Device_CompatibleIds, "Device.CompatibleIds"));

        record.installed_driver.description = property_string(
            read(DEVPKEY_Device_DriverDesc, "Device.DriverDesc"));
        record.installed_driver.provider = property_string(
            read(DEVPKEY_Device_DriverProvider, "Device.DriverProvider"));
        record.installed_driver.version = property_string(
            read(DEVPKEY_Device_DriverVersion, "Device.DriverVersion"));
        record.installed_driver.inf_path = property_string(
            read(DEVPKEY_Device_DriverInfPath, "Device.DriverInfPath"));
        record.installed_driver.matching_device_id = property_string(
            read(DEVPKEY_Device_MatchingDeviceId, "Device.MatchingDeviceId"));
        record.installed_driver.date_yyyymmdd = property_date(
            read(DEVPKEY_Device_DriverDate, "Device.DriverDate"));

        record.present = property_bool(
            read(DEVPKEY_Device_IsPresent, "Device.IsPresent"), options.present_only);

        ULONG status = 0;
        ULONG problem = 0;
        const CONFIGRET config_result = CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0);
        if (config_result == CR_SUCCESS) {
            record.status_flags = status;
            record.problem_code = problem;
        } else {
            result.property_errors.push_back({
                .operation = "CM_Get_DevNode_Status",
                .device_instance_id = record.instance_id,
                .windows_error = config_result,
                .message = "Configuration Manager error " + std::to_string(config_result),
            });
        }

        result.devices.push_back(std::move(record));
    }

    std::sort(result.devices.begin(), result.devices.end(), [](const auto& left, const auto& right) {
        return left.instance_id < right.instance_id;
    });
    return result;
}

bool write_inventory_json(
    const DeviceInventoryResult& inventory,
    const std::string& output_path,
    std::string& error_message) {
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error_message = "could not open output file: " + output_path;
        return false;
    }

    output << "{\n  \"schema_version\": 1,\n  \"device_count\": "
           << inventory.devices.size() << ",\n  \"devices\": [";
    for (std::size_t index = 0; index < inventory.devices.size(); ++index) {
        const auto& device = inventory.devices[index];
        output << (index == 0 ? "\n" : ",\n") << "    {\n";
        output << "      \"instance_id\": "; write_string(output, device.instance_id); output << ",\n";
        output << "      \"class_name\": "; write_string(output, device.class_name); output << ",\n";
        output << "      \"class_guid\": "; write_string(output, device.class_guid); output << ",\n";
        output << "      \"description\": "; write_string(output, device.description); output << ",\n";
        output << "      \"friendly_name\": "; write_string(output, device.friendly_name); output << ",\n";
        output << "      \"manufacturer\": "; write_string(output, device.manufacturer); output << ",\n";
        output << "      \"location\": "; write_string(output, device.location); output << ",\n";
        output << "      \"hardware_ids\": "; write_string_array(output, device.hardware_ids); output << ",\n";
        output << "      \"compatible_ids\": "; write_string_array(output, device.compatible_ids); output << ",\n";
        output << "      \"present\": " << (device.present ? "true" : "false") << ",\n";
        output << "      \"status_flags\": " << device.status_flags << ",\n";
        output << "      \"problem_code\": " << device.problem_code << ",\n";
        output << "      \"installed_driver\": {\n";
        output << "        \"description\": "; write_string(output, device.installed_driver.description); output << ",\n";
        output << "        \"provider\": "; write_string(output, device.installed_driver.provider); output << ",\n";
        output << "        \"version\": "; write_string(output, device.installed_driver.version); output << ",\n";
        output << "        \"inf_path\": "; write_string(output, device.installed_driver.inf_path); output << ",\n";
        output << "        \"matching_device_id\": "; write_string(output, device.installed_driver.matching_device_id); output << ",\n";
        output << "        \"date_yyyymmdd\": " << device.installed_driver.date_yyyymmdd << "\n";
        output << "      }\n    }";
    }
    output << "\n  ],\n  \"property_errors\": [";
    for (std::size_t index = 0; index < inventory.property_errors.size(); ++index) {
        const auto& error = inventory.property_errors[index];
        output << (index == 0 ? "\n" : ",\n") << "    {\"operation\": ";
        write_string(output, error.operation);
        output << ", \"device_instance_id\": ";
        write_string(output, error.device_instance_id);
        output << ", \"code\": " << error.windows_error << ", \"message\": ";
        write_string(output, error.message);
        output << '}';
    }
    output << "\n  ]";

    if (inventory.fatal_error) {
        output << ",\n  \"fatal_error\": {\"operation\": ";
        write_string(output, inventory.fatal_error->operation);
        output << ", \"code\": " << inventory.fatal_error->windows_error
               << ", \"message\": ";
        write_string(output, inventory.fatal_error->message);
        output << '}';
    } else {
        output << ",\n  \"fatal_error\": null";
    }
    output << "\n}\n";

    if (!output) {
        error_message = "failed while writing output file: " + output_path;
        return false;
    }
    return true;
}

}  // namespace open_driver

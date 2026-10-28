// Language: C++20; file: local_driver_store.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>
#include <bcrypt.h>
#include <setupapi.h>

#include "local_driver_store.hpp"
#include "external_package_source.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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

class DriverInfoList final {
public:
    DriverInfoList(HDEVINFO set, SP_DEVINFO_DATA* device) : set_(set), device_(device) {}
    ~DriverInfoList() {
        if (built_) {
            SetupDiDestroyDriverInfoList(set_, device_, SPDIT_COMPATDRIVER);
        }
    }
    void mark_built() { built_ = true; }

private:
    HDEVINFO set_{};
    SP_DEVINFO_DATA* device_{};
    bool built_{};
};

std::wstring wide_from_utf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
}

std::string utf8_from_wide(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }
    const int length = static_cast<int>(std::wcslen(value));
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, length, result.data(), required, nullptr, nullptr);
    return result;
}

std::string windows_error_message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (size == 0 || buffer == nullptr) {
        return "Windows error " + std::to_string(error);
    }
    std::wstring message(buffer, size);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    const auto converted = utf8_from_wide(message.c_str());
    return converted.empty() ? "Windows error " + std::to_string(error) : converted;
}

CandidateSourceError make_error(std::string operation, DWORD error) {
    return {
        .operation = std::move(operation),
        .windows_error = error,
        .message = windows_error_message(error),
    };
}

std::uint32_t date_yyyymmdd(const FILETIME& file_time) {
    SYSTEMTIME system_time{};
    if (!FileTimeToSystemTime(&file_time, &system_time)) {
        return 0;
    }
    return static_cast<std::uint32_t>(system_time.wYear) * 10000U +
           static_cast<std::uint32_t>(system_time.wMonth) * 100U +
           static_cast<std::uint32_t>(system_time.wDay);
}

DriverVersion split_version(DWORDLONG version) {
    return {{
        static_cast<std::uint16_t>((version >> 48U) & 0xffffU),
        static_cast<std::uint16_t>((version >> 32U) & 0xffffU),
        static_cast<std::uint16_t>((version >> 16U) & 0xffffU),
        static_cast<std::uint16_t>(version & 0xffffU),
    }};
}

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string generic_bus_hardware_id(std::string value) {
    value = upper_ascii(std::move(value));
    if (!value.starts_with("PCI\\") && !value.starts_with("USB\\")) return {};

    std::size_t cut = value.size();
    for (const auto* marker : {"&SUBSYS_", "&REV_", "&CC_", "&MI_"}) {
        const auto position = value.find(marker);
        if (position != std::string::npos) cut = std::min(cut, position);
    }
    value.resize(cut);

    const bool pci_identity = value.starts_with("PCI\\VEN_") &&
        value.find("&DEV_") != std::string::npos;
    const bool usb_identity = value.starts_with("USB\\VID_") &&
        value.find("&PID_") != std::string::npos;
    return pci_identity || usb_identity ? value : std::string{};
}

void select_matching_id(
    const DeviceRecord& device,
    const std::string& inf_hardware_id,
    const std::vector<std::string>& inf_compatible_ids,
    Candidate& candidate) {
    std::vector<std::string> inf_ids;
    if (!inf_hardware_id.empty()) {
        inf_ids.push_back(upper_ascii(inf_hardware_id));
    }
    for (const auto& id : inf_compatible_ids) {
        inf_ids.push_back(upper_ascii(id));
    }

    const auto find_match = [&](const std::vector<std::string>& device_ids, MatchKind kind) {
        for (const auto& device_id : device_ids) {
            const auto normalized = upper_ascii(device_id);
            if (std::find(inf_ids.begin(), inf_ids.end(), normalized) != inf_ids.end()) {
                candidate.matching_id = device_id;
                candidate.match_kind = kind;
                return true;
            }
        }
        return false;
    };

    if (find_match(device.hardware_ids, MatchKind::hardware_id)) {
        return;
    }

    // Vendor packages often list PCI/USB VEN+DEV or VID+PID while Windows
    // reports the same device with SUBSYS/REV/MI suffixes. Keep the complete
    // installed ID so all later gates still bind the package to this device.
    for (const auto& device_id : device.hardware_ids) {
        const auto generic_device_id = generic_bus_hardware_id(device_id);
        if (generic_device_id.empty()) continue;
        if (std::find(inf_ids.begin(), inf_ids.end(), generic_device_id) != inf_ids.end()) {
            candidate.matching_id = device_id;
            candidate.match_kind = MatchKind::hardware_id;
            return;
        }
    }

    if (find_match(device.compatible_ids, MatchKind::compatible_id)) return;

    candidate.matching_id = inf_hardware_id;
    candidate.match_kind = MatchKind::compatible_id;
}

std::vector<std::string> parse_compatible_ids(const SP_DRVINFO_DETAIL_DATA_W& detail) {
    std::vector<std::string> values;
    if (detail.CompatIDsLength == 0) {
        return values;
    }

    const wchar_t* cursor = detail.HardwareID + detail.CompatIDsOffset;
    const wchar_t* end = cursor + detail.CompatIDsLength;
    while (cursor < end && *cursor != L'\0') {
        const wchar_t* terminator = std::find(cursor, end, L'\0');
        values.push_back(utf8_from_wide(std::wstring(cursor, terminator).c_str()));
        if (terminator == end) {
            break;
        }
        cursor = terminator + 1;
    }
    return values;
}

std::string sha256_file(const std::wstring& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    DWORD object_size = 0;
    DWORD result_size = 0;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return {};
    }

    const auto close_algorithm = std::unique_ptr<void, decltype([](void* handle) {
        if (handle != nullptr) {
            BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
        }
    })>(algorithm, {});

    if (BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size), &result_size, 0) < 0) {
        return {};
    }
    object.resize(object_size);
    if (BCryptCreateHash(
            algorithm, &hash, object.data(), static_cast<ULONG>(object.size()),
            nullptr, 0, 0) < 0) {
        return {};
    }

    const auto destroy_hash = std::unique_ptr<void, decltype([](void* handle) {
        if (handle != nullptr) {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
        }
    })>(hash, {});

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count), 0) < 0) {
            return {};
        }
    }
    if (!input.eof()) {
        return {};
    }
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        return {};
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

}  // namespace

void select_candidate_matching_id(
    const DeviceRecord& device,
    const std::string& inf_hardware_id,
    const std::vector<std::string>& inf_compatible_ids,
    Candidate& candidate) {
    select_matching_id(device, inf_hardware_id, inf_compatible_ids, candidate);
}

static LocalCandidateResult discover_driver_candidates(
    const DeviceRecord& device_record,
    const std::optional<std::string>& external_inf_path) {
    LocalCandidateResult result;
    result.device_instance_id = device_record.instance_id;

    DeviceInfoSet set(SetupDiCreateDeviceInfoList(nullptr, nullptr));
    if (set.get() == INVALID_HANDLE_VALUE) {
        result.fatal_error = make_error("SetupDiCreateDeviceInfoList", GetLastError());
        return result;
    }

    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    const auto instance_id = wide_from_utf8(device_record.instance_id);
    if (instance_id.empty() || !SetupDiOpenDeviceInfoW(
            set.get(), instance_id.c_str(), nullptr, 0, &device)) {
        result.fatal_error = make_error("SetupDiOpenDeviceInfoW", GetLastError());
        return result;
    }

    SP_DEVINSTALL_PARAMS_W device_params{};
    device_params.cbSize = sizeof(device_params);
    if (!SetupDiGetDeviceInstallParamsW(set.get(), &device, &device_params)) {
        result.fatal_error = make_error("SetupDiGetDeviceInstallParamsW", GetLastError());
        return result;
    }
    device_params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;

    if (external_inf_path) {
        const auto requested_path = wide_from_utf8(*external_inf_path);
        if (requested_path.empty()) {
            result.fatal_error = make_error("external INF path", ERROR_INVALID_NAME);
            return result;
        }

        std::array<wchar_t, 32768> full_path_buffer{};
        const DWORD full_path_length = GetFullPathNameW(
            requested_path.c_str(), static_cast<DWORD>(full_path_buffer.size()),
            full_path_buffer.data(), nullptr);
        if (full_path_length == 0) {
            result.fatal_error = make_error("GetFullPathNameW", GetLastError());
            return result;
        }
        if (full_path_length >= full_path_buffer.size()) {
            result.fatal_error = make_error("GetFullPathNameW", ERROR_FILENAME_EXCED_RANGE);
            return result;
        }
        const std::wstring full_path(full_path_buffer.data(), full_path_length);
        const DWORD attributes = GetFileAttributesW(full_path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            result.fatal_error = make_error("external INF file", ERROR_FILE_NOT_FOUND);
            return result;
        }
        if (full_path.size() >= std::size(device_params.DriverPath)) {
            result.fatal_error = make_error("external INF path", ERROR_FILENAME_EXCED_RANGE);
            return result;
        }

        const auto extension_position = full_path.find_last_of(L'.');
        std::wstring extension = extension_position == std::wstring::npos
            ? std::wstring{}
            : full_path.substr(extension_position);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".inf") {
            result.fatal_error = make_error("external INF extension", ERROR_INVALID_NAME);
            return result;
        }

        wcscpy_s(device_params.DriverPath, full_path.c_str());
        device_params.Flags |= DI_ENUMSINGLEINF;
    }

    if (!SetupDiSetDeviceInstallParamsW(set.get(), &device, &device_params)) {
        result.fatal_error = make_error("SetupDiSetDeviceInstallParamsW", GetLastError());
        return result;
    }

    DriverInfoList list(set.get(), &device);
    if (!SetupDiBuildDriverInfoList(set.get(), &device, SPDIT_COMPATDRIVER)) {
        result.fatal_error = make_error("SetupDiBuildDriverInfoList", GetLastError());
        return result;
    }
    list.mark_built();

    for (DWORD index = 0;; ++index) {
        SP_DRVINFO_DATA_W driver{};
        driver.cbSize = sizeof(driver);
        if (!SetupDiEnumDriverInfoW(
                set.get(), &device, SPDIT_COMPATDRIVER, index, &driver)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                result.errors.push_back(make_error("SetupDiEnumDriverInfoW", error));
            }
            break;
        }

        SP_DRVINSTALL_PARAMS install_params{};
        install_params.cbSize = sizeof(install_params);
        if (!SetupDiGetDriverInstallParamsW(
                set.get(), &device, &driver, &install_params)) {
            result.errors.push_back(make_error("SetupDiGetDriverInstallParamsW", GetLastError()));
            continue;
        }

        DWORD required = 0;
        SetupDiGetDriverInfoDetailW(
            set.get(), &device, &driver, nullptr, 0, &required);
        const DWORD detail_error = GetLastError();
        if (detail_error != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DRVINFO_DETAIL_DATA_W)) {
            result.errors.push_back(make_error("SetupDiGetDriverInfoDetailW(size)", detail_error));
            continue;
        }

        std::vector<std::byte> detail_buffer(required);
        auto* detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(detail_buffer.data());
        detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
        if (!SetupDiGetDriverInfoDetailW(
                set.get(), &device, &driver, detail, required, &required)) {
            result.errors.push_back(make_error("SetupDiGetDriverInfoDetailW", GetLastError()));
            continue;
        }

        Candidate candidate;
        candidate.package_id = utf8_from_wide(detail->InfFileName) + "#" +
                               utf8_from_wide(detail->SectionName);
        candidate.package_hash = sha256_file(detail->InfFileName);
        if (candidate.package_hash.empty()) {
            candidate.package_hash = candidate.package_id;
        }
        candidate.windows_rank = install_params.Rank;
        candidate.driver_date = date_yyyymmdd(driver.DriverDate);
        candidate.driver_version = split_version(driver.DriverVersion);
        candidate.architecture_applicable = true;
        candidate.os_applicable = true;
        candidate.inf_signed = (install_params.Flags & DNF_INF_IS_SIGNED) != 0;
        candidate.authenticode_signed =
            (install_params.Flags & DNF_AUTHENTICODE_SIGNED) != 0;
        candidate.signature_valid = candidate.inf_signed;
        candidate.blocked_by_windows = (install_params.Flags & DNF_BAD_DRIVER) != 0;
        candidate.currently_installed =
            (install_params.Flags & DNF_INSTALLEDDRIVER) != 0;
        candidate.provider = utf8_from_wide(driver.ProviderName);
        candidate.description = utf8_from_wide(driver.Description);
        candidate.inf_path = utf8_from_wide(detail->InfFileName);
        candidate.section_name = utf8_from_wide(detail->SectionName);
        candidate.driver_node_flags = install_params.Flags;
        candidate.inf_hardware_id = detail->CompatIDsOffset > 1
            ? utf8_from_wide(detail->HardwareID)
            : std::string{};
        candidate.inf_compatible_ids = parse_compatible_ids(*detail);
        select_candidate_matching_id(
            device_record, candidate.inf_hardware_id, candidate.inf_compatible_ids,
            candidate);

        result.candidates.push_back(std::move(candidate));
    }

    return result;
}

LocalCandidateResult discover_local_driver_candidates(const DeviceRecord& device_record) {
    return discover_driver_candidates(device_record, std::nullopt);
}

LocalCandidateResult analyze_external_inf_for_device(
    const DeviceRecord& device,
    const std::string& inf_path) {
    return discover_driver_candidates(device, inf_path);
}

}  // namespace open_driver

// Language: C++20; file: package_materializer.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>
#include <setupapi.h>
#include <softpub.h>
#include <wintrust.h>

#include "package_materializer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace open_driver {
namespace {

CatalogError make_error(std::string operation, DWORD error) {
    return {.operation = std::move(operation), .windows_error = error,
            .message = "Windows error " + std::to_string(error)};
}

std::string utf8_from_wide(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, nullptr);
    return result;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::wstring lower_wide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    return value;
}

std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

bool extract_zip_with_windows_tar(
    const std::filesystem::path& archive,
    const std::filesystem::path& destination,
    DWORD& error) {
    std::wstring command = L"tar.exe -xf " + quote_windows_argument(archive.wstring()) +
                           L" -C " + quote_windows_argument(destination.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = GetLastError();
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 10 * 60 * 1000);
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit_code);
    else if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        exit_code = ERROR_TIMEOUT;
    }
    CloseHandle(process.hProcess);
    error = exit_code;
    return wait == WAIT_OBJECT_0 && exit_code == 0;
}

bool extract_executable_with_7zip(
    const std::filesystem::path& archive,
    const std::filesystem::path& destination,
    DWORD& error) {
    std::filesystem::path seven_zip = L"C:\\Program Files\\7-Zip\\7z.exe";
    if (!std::filesystem::is_regular_file(seven_zip)) {
        seven_zip = L"C:\\Program Files (x86)\\7-Zip\\7z.exe";
    }
    if (!std::filesystem::is_regular_file(seven_zip)) {
        error = ERROR_FILE_NOT_FOUND;
        return false;
    }
    std::wstring command = quote_windows_argument(seven_zip.wstring()) + L" x -y -o" +
                           quote_windows_argument(destination.wstring()) + L" " +
                           quote_windows_argument(archive.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(seven_zip.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = GetLastError();
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 30 * 60 * 1000);
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit_code);
    else if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        exit_code = ERROR_TIMEOUT;
    }
    CloseHandle(process.hProcess);
    error = exit_code;
    return wait == WAIT_OBJECT_0 && exit_code == 0;
}

struct CabinetContext {
    std::filesystem::path root;
    std::vector<std::filesystem::path>* extracted{};
    DWORD error{};
};

UINT CALLBACK cabinet_callback(PVOID raw_context, UINT notification, UINT_PTR parameter1, UINT_PTR) {
    auto& context = *static_cast<CabinetContext*>(raw_context);
    if (notification == SPFILENOTIFY_FILEINCABINET) {
        auto* information = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(parameter1);
        const std::wstring relative = information->NameInCabinet == nullptr
            ? std::wstring{} : std::wstring(information->NameInCabinet);
        if (!is_safe_cabinet_relative_path(relative)) {
            context.error = ERROR_INVALID_NAME;
            return FILEOP_ABORT;
        }
        const auto target = context.root / std::filesystem::path(relative);
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error || target.wstring().size() >= MAX_PATH) {
            context.error = error ? static_cast<DWORD>(error.value()) : ERROR_FILENAME_EXCED_RANGE;
            return FILEOP_ABORT;
        }
        wcscpy_s(information->FullTargetName, target.c_str());
        context.extracted->push_back(target);
        return FILEOP_DOIT;
    }
    if (notification == SPFILENOTIFY_NEEDNEWCABINET) {
        context.error = ERROR_NOT_SUPPORTED;
        return FILEOP_ABORT;
    }
    return NO_ERROR;
}

bool verify_catalog_signature(const std::filesystem::path& path, LONG& status) {
    WINTRUST_FILE_INFO file_info{};
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = path.c_str();

    WINTRUST_DATA trust_data{};
    trust_data.cbStruct = sizeof(trust_data);
    trust_data.dwUIChoice = WTD_UI_NONE;
    trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust_data.dwUnionChoice = WTD_CHOICE_FILE;
    trust_data.pFile = &file_info;
    trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
    trust_data.dwProvFlags = 0;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    status = WinVerifyTrust(nullptr, &policy, &trust_data);
    trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trust_data);
    return status == ERROR_SUCCESS;
}

std::vector<std::string> catalog_references(const std::filesystem::path& inf) {
    std::ifstream input(inf, std::ios::binary);
    if (!input) return {};
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (content.size() >= 2 && static_cast<unsigned char>(content[0]) == 0xff &&
        static_cast<unsigned char>(content[1]) == 0xfe) {
        const auto* start = reinterpret_cast<const wchar_t*>(content.data() + 2);
        const std::size_t count = (content.size() - 2) / sizeof(wchar_t);
        content = utf8_from_wide(std::wstring(start, count));
    }
    std::vector<std::string> values;
    const std::regex expression(
        "(?:^|[\\r\\n])\\s*CatalogFile(?:\\.[^=\\r\\n]+)?\\s*=\\s*([^;\\r\\n]+)",
        std::regex::icase);
    for (auto iterator = std::sregex_iterator(content.begin(), content.end(), expression);
         iterator != std::sregex_iterator(); ++iterator) {
        std::string value = (*iterator)[1].str();
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
            return std::isspace(c) != 0 || c == '"';
        }));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
            return std::isspace(c) != 0 || c == '"';
        }).base(), value.end());
        if (!value.empty()) values.push_back(value);
    }
    return values;
}

CatalogDownloadResult import_catalog_file(
    const CatalogFile& file,
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    CatalogDownloadResult result;
    result.path = destination;
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        result.error = make_error("source package file", error ? error.value() : ERROR_FILE_NOT_FOUND);
        return result;
    }
    result.size_bytes = std::filesystem::file_size(source, error);
    if (error) {
        result.error = make_error("read source package size", error.value());
        return result;
    }
    result.sha256_base64 = sha256_file_base64(source);
    result.hash_matches = !file.sha256_base64.empty() &&
                          result.sha256_base64 == file.sha256_base64;
    if (!result.hash_matches) {
        result.error = make_error("SHA-256 verification", ERROR_CRC);
        return result;
    }
    if (!std::filesystem::copy_file(
            source, destination, std::filesystem::copy_options::none, error)) {
        result.error = make_error(
            "copy verified package", error ? error.value() : ERROR_WRITE_FAULT);
    }
    return result;
}

}  // namespace

bool is_safe_cabinet_relative_path(const std::wstring& value) {
    if (value.empty()) return false;
    std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& component : path) {
        if (component == L".." || component == L"." || component.empty()) return false;
    }
    return value.find(L':') == std::wstring::npos;
}

bool verify_catalog_signature_file(
    const std::filesystem::path& path,
    std::string& status) {
    LONG trust_status = TRUST_E_NOSIGNATURE;
    const bool valid = verify_catalog_signature(path, trust_status);
    std::ostringstream text;
    text << "0x" << std::hex << static_cast<unsigned long>(trust_status);
    status = text.str();
    return valid;
}

PackageMaterializationResult materialize_catalog_package(
    const std::vector<CatalogFile>& files,
    const std::filesystem::path& output_directory,
    const std::optional<std::filesystem::path>& source_directory,
    bool accept_computed_archive_hash) {
    PackageMaterializationResult result;
    if (files.empty()) {
        result.error = make_error("catalog package has no files", ERROR_NOT_FOUND);
        return result;
    }
    if (std::filesystem::exists(output_directory)) {
        result.error = make_error("output directory already exists", ERROR_ALREADY_EXISTS);
        return result;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_directory / L"downloads", filesystem_error);
    if (filesystem_error) {
        result.error = make_error("create output directory", filesystem_error.value());
        return result;
    }

    std::filesystem::create_directories(output_directory / L"extracted", filesystem_error);
    if (filesystem_error) {
        result.error = make_error("create extraction root", filesystem_error.value());
        return result;
    }

    for (const auto& file : files) {
        const auto name = std::filesystem::path(file.file_name).filename();
        if (name.empty() || name != std::filesystem::path(file.file_name)) {
            result.error = make_error("unsafe catalog file name", ERROR_INVALID_NAME);
            return result;
        }
        const auto destination = output_directory / L"downloads" / name;
        auto download = source_directory
            ? import_catalog_file(file, *source_directory / name, destination)
            : download_catalog_file(file, destination, accept_computed_archive_hash);
        result.downloads.push_back(download);
        if (download.error) {
            result.error = download.error;
            return result;
        }

        const auto extension = lower_wide(name.extension().wstring());
        if (extension == L".cab") {
            const auto extraction_root = output_directory / L"extracted" / name.stem();
            std::filesystem::create_directories(extraction_root, filesystem_error);
            if (filesystem_error) {
                result.error = make_error("create extraction directory", filesystem_error.value());
                return result;
            }
            CabinetContext context{.root = extraction_root, .extracted = &result.extracted_files};
            if (!SetupIterateCabinetW(download.path.c_str(), 0, cabinet_callback, &context)) {
                result.error = make_error(
                    "SetupIterateCabinetW", context.error != 0 ? context.error : GetLastError());
                return result;
            }
        } else if (extension == L".zip") {
            const auto extraction_root = output_directory / L"extracted" / name.stem();
            std::filesystem::create_directories(extraction_root, filesystem_error);
            if (filesystem_error) {
                result.error = make_error("create extraction directory", filesystem_error.value());
                return result;
            }
            DWORD extraction_error = ERROR_SUCCESS;
            if (!extract_zip_with_windows_tar(download.path, extraction_root, extraction_error)) {
                result.error = make_error("extract ZIP with Windows tar", extraction_error);
                return result;
            }
        } else if (extension == L".exe") {
            const auto extraction_root = output_directory / L"extracted" / name.stem();
            std::filesystem::create_directories(extraction_root, filesystem_error);
            if (filesystem_error) {
                result.error = make_error("create extraction directory", filesystem_error.value());
                return result;
            }
            std::string signature_status;
            if (!verify_catalog_signature_file(download.path, signature_status)) {
                result.error = make_error(
                    "verify signed executable", static_cast<DWORD>(TRUST_E_SUBJECT_NOT_TRUSTED));
                return result;
            }
            DWORD extraction_error = ERROR_SUCCESS;
            if (!extract_executable_with_7zip(download.path, extraction_root, extraction_error)) {
                result.error = make_error("extract signed executable with 7-Zip", extraction_error);
                return result;
            }
        }
    }

    std::vector<std::filesystem::path> inf_paths;
    std::vector<std::filesystem::path> catalog_paths;
    const auto extracted_root = output_directory / L"extracted";
    for (std::filesystem::recursive_directory_iterator iterator(
             extracted_root, std::filesystem::directory_options::skip_permission_denied,
             filesystem_error), end;
         iterator != end && !filesystem_error; iterator.increment(filesystem_error)) {
        std::error_code entry_error;
        if (!iterator->is_regular_file(entry_error)) {
            if (entry_error) filesystem_error = entry_error;
            continue;
        }
        const auto extension = lower_wide(iterator->path().extension().wstring());
        if (extension == L".cat") catalog_paths.push_back(iterator->path());
        else if (extension == L".inf") inf_paths.push_back(iterator->path());
    }
    if (filesystem_error) {
        result.error = make_error("enumerate extracted package", filesystem_error.value());
        return result;
    }
    for (const auto& path : catalog_paths) {
        LONG status = TRUST_E_NOSIGNATURE;
        const bool valid = verify_catalog_signature(path, status);
        std::ostringstream text;
        text << "0x" << std::hex << static_cast<unsigned long>(status);
        result.catalogs.push_back({.path = path, .valid = valid, .status = text.str()});
    }
    for (const auto& path : inf_paths) {
        InfPackageRecord record;
        record.path = path;
        record.catalog_file_names = catalog_references(path);
        record.referenced_catalogs_present = !record.catalog_file_names.empty();
        for (const auto& reference : record.catalog_file_names) {
            const auto expected = lower_ascii(reference);
            const bool present = std::any_of(result.catalogs.begin(), result.catalogs.end(),
                [&](const auto& catalog) {
                    return lower_ascii(catalog.path.filename().string()) == expected;
                });
            record.referenced_catalogs_present = record.referenced_catalogs_present && present;
        }
        result.inf_files.push_back(std::move(record));
    }
    if (result.inf_files.empty()) {
        result.error = make_error("locate INF", ERROR_NOT_FOUND);
    } else if (result.catalogs.empty()) {
        result.error = make_error("locate catalog", ERROR_NOT_FOUND);
    } else if (std::any_of(result.catalogs.begin(), result.catalogs.end(),
                           [](const auto& catalog) { return !catalog.valid; })) {
        result.error = make_error(
            "verify catalog signature", static_cast<DWORD>(TRUST_E_SUBJECT_NOT_TRUSTED));
    } else if (std::any_of(result.inf_files.begin(), result.inf_files.end(),
                           [](const auto& inf) { return !inf.referenced_catalogs_present; })) {
        result.error = make_error("verify INF catalog references", ERROR_INVALID_DATA);
    }
    return result;
}

}  // namespace open_driver

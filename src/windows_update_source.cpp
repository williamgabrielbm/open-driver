// Language: C++20; file: windows_update_source.cpp; target: Windows 11; runtime: MSVC/CMake.
#include <windows.h>
#include <oleauto.h>
#include <wuapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include "windows_update_source.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace open_driver {
namespace {

using Microsoft::WRL::ComPtr;

class SearchCompletedCallback final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          ISearchCompletedCallback> {
public:
    SearchCompletedCallback() : completed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~SearchCompletedCallback() override {
        if (completed_) CloseHandle(completed_);
    }
    HRESULT STDMETHODCALLTYPE Invoke(ISearchJob*, ISearchCompletedCallbackArgs*) override {
        return completed_ && SetEvent(completed_) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    [[nodiscard]] HANDLE completed() const { return completed_; }

private:
    HANDLE completed_{};
};

class ComApartment final {
public:
    ComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns_ = SUCCEEDED(result_);
        if (result_ == RPC_E_CHANGED_MODE) {
            result_ = S_OK;
        }
    }
    ~ComApartment() {
        if (owns_) {
            CoUninitialize();
        }
    }
    [[nodiscard]] HRESULT result() const { return result_; }

private:
    HRESULT result_{E_FAIL};
    bool owns_{};
};

class ScopedBstr final {
public:
    ScopedBstr() = default;
    explicit ScopedBstr(const wchar_t* value) : value_(SysAllocString(value)) {}
    ~ScopedBstr() { SysFreeString(value_); }
    ScopedBstr(const ScopedBstr&) = delete;
    ScopedBstr& operator=(const ScopedBstr&) = delete;
    [[nodiscard]] BSTR get() const { return value_; }
    [[nodiscard]] BSTR* put() {
        SysFreeString(value_);
        value_ = nullptr;
        return &value_;
    }

private:
    BSTR value_{};
};

std::string utf8_from_bstr(BSTR value) {
    if (value == nullptr || SysStringLen(value) == 0) {
        return {};
    }
    const int length = static_cast<int>(SysStringLen(value));
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, length, result.data(), required,
        nullptr, nullptr);
    return result;
}

std::string hresult_message(HRESULT result) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<wchar_t*>(&buffer),
        0, nullptr);
    if (size != 0 && buffer != nullptr) {
        std::wstring message(buffer, size);
        LocalFree(buffer);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
            message.pop_back();
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, message.data(), static_cast<int>(message.size()),
            nullptr, 0, nullptr, nullptr);
        if (required > 0) {
            std::string converted(static_cast<std::size_t>(required), '\0');
            WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, message.data(), static_cast<int>(message.size()),
                converted.data(), required, nullptr, nullptr);
            return converted;
        }
    }

    std::ostringstream output;
    output << "HRESULT 0x" << std::hex << std::setw(8) << std::setfill('0')
           << static_cast<std::uint32_t>(result);
    return output.str();
}

WindowsUpdateError make_error(std::string operation, HRESULT result) {
    return {
        .operation = std::move(operation),
        .hresult = result,
        .message = hresult_message(result),
    };
}

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

struct InstanceMatches {
    std::vector<std::string> all;
    std::vector<std::string> present;
};

InstanceMatches matching_instances(
    const std::vector<DeviceRecord>& inventory,
    const std::string& hardware_id) {
    InstanceMatches result;
    const auto target = upper_ascii(hardware_id);
    if (target.empty()) {
        return result;
    }

    for (const auto& device : inventory) {
        const auto matches = [&](const std::vector<std::string>& ids) {
            return std::any_of(ids.begin(), ids.end(), [&](const auto& id) {
                return upper_ascii(id) == target;
            });
        };
        if (matches(device.hardware_ids) || matches(device.compatible_ids)) {
            result.all.push_back(device.instance_id);
            if (device.present) {
                result.present.push_back(device.instance_id);
            }
        }
    }
    return result;
}

std::uint32_t date_yyyymmdd(DATE date) {
    SYSTEMTIME value{};
    if (!VariantTimeToSystemTime(date, &value)) {
        return 0;
    }
    return static_cast<std::uint32_t>(value.wYear) * 10000U +
           static_cast<std::uint32_t>(value.wMonth) * 100U +
           static_cast<std::uint32_t>(value.wDay);
}

template <typename Getter>
std::string read_bstr(Getter getter) {
    ScopedBstr value;
    return SUCCEEDED(getter(value.put())) ? utf8_from_bstr(value.get()) : std::string{};
}

}  // namespace

WindowsUpdateSearchResult search_windows_update_drivers(
    const std::vector<DeviceRecord>& inventory,
    bool online,
    std::stop_token stop) {
    WindowsUpdateSearchResult result;
    if (stop.stop_requested()) {
        result.cancelled = true;
        return result;
    }
    ComApartment apartment;
    if (FAILED(apartment.result())) {
        result.fatal_error = make_error("CoInitializeEx", apartment.result());
        return result;
    }

    ComPtr<IUpdateSession> session;
    HRESULT status = CoCreateInstance(
        CLSID_UpdateSession, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&session));
    if (FAILED(status)) {
        result.fatal_error = make_error("CoCreateInstance(UpdateSession)", status);
        return result;
    }

    ScopedBstr client_id(L"OpenDriverCore");
    status = session->put_ClientApplicationID(client_id.get());
    if (FAILED(status)) {
        result.fatal_error = make_error("IUpdateSession::put_ClientApplicationID", status);
        return result;
    }

    ComPtr<IUpdateSearcher> searcher;
    status = session->CreateUpdateSearcher(&searcher);
    if (FAILED(status)) {
        result.fatal_error = make_error("IUpdateSession::CreateUpdateSearcher", status);
        return result;
    }
    searcher->put_CanAutomaticallyUpgradeService(VARIANT_FALSE);
    searcher->put_Online(online ? VARIANT_TRUE : VARIANT_FALSE);

    ScopedBstr criteria(L"IsInstalled=0 and IsHidden=0 and Type='Driver'");
    auto callback = Microsoft::WRL::Make<SearchCompletedCallback>();
    if (!callback || !callback->completed()) {
        result.fatal_error = make_error("CreateEvent(Windows Update)", HRESULT_FROM_WIN32(GetLastError()));
        return result;
    }
    VARIANT state;
    VariantInit(&state);
    ComPtr<ISearchJob> search_job;
    status = searcher->BeginSearch(criteria.get(), callback.Get(), state, &search_job);
    if (FAILED(status)) {
        result.fatal_error = make_error("IUpdateSearcher::BeginSearch", status);
        return result;
    }
    while (WaitForSingleObject(callback->completed(), 50) == WAIT_TIMEOUT) {
        if (!stop.stop_requested()) continue;
        search_job->RequestAbort();
        WaitForSingleObject(callback->completed(), 5000);
        result.cancelled = true;
        return result;
    }
    if (stop.stop_requested()) {
        search_job->RequestAbort();
        result.cancelled = true;
        return result;
    }
    ComPtr<ISearchResult> search_result;
    status = searcher->EndSearch(search_job.Get(), &search_result);
    if (FAILED(status)) {
        result.fatal_error = make_error("IUpdateSearcher::EndSearch", status);
        return result;
    }

    OperationResultCode operation_result{};
    status = search_result->get_ResultCode(&operation_result);
    if (FAILED(status)) {
        result.fatal_error = make_error("ISearchResult::get_ResultCode", status);
        return result;
    }
    result.operation_result_code = static_cast<std::int32_t>(operation_result);
    if (operation_result != orcSucceeded) {
        result.fatal_error = WindowsUpdateError{
            .operation = "IUpdateSearcher::Search result",
            .hresult = static_cast<std::int32_t>(operation_result),
            .message = "a busca terminou sem sucesso completo (código " +
                       std::to_string(static_cast<std::int32_t>(operation_result)) + ")",
        };
        return result;
    }

    ComPtr<IUpdateCollection> updates;
    status = search_result->get_Updates(&updates);
    if (FAILED(status)) {
        result.fatal_error = make_error("ISearchResult::get_Updates", status);
        return result;
    }

    LONG count = 0;
    status = updates->get_Count(&count);
    if (FAILED(status)) {
        result.fatal_error = make_error("IUpdateCollection::get_Count", status);
        return result;
    }

    for (LONG index = 0; index < count; ++index) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            result.updates.clear();
            return result;
        }
        ComPtr<IUpdate> update;
        status = updates->get_Item(index, &update);
        if (FAILED(status)) {
            result.fatal_error = make_error("IUpdateCollection::get_Item", status);
            result.updates.clear();
            return result;
        }

        ComPtr<IWindowsDriverUpdate> driver;
        status = update.As(&driver);
        if (FAILED(status)) {
            result.fatal_error = make_error("IUpdate::QueryInterface(IWindowsDriverUpdate)", status);
            result.updates.clear();
            return result;
        }

        WindowsUpdateDriverRecord record;
        record.title = read_bstr([&](BSTR* value) { return update->get_Title(value); });
        record.description = read_bstr([&](BSTR* value) { return update->get_Description(value); });
        record.driver_class = read_bstr([&](BSTR* value) { return driver->get_DriverClass(value); });
        record.driver_hardware_id = read_bstr(
            [&](BSTR* value) { return driver->get_DriverHardwareID(value); });
        record.manufacturer = read_bstr(
            [&](BSTR* value) { return driver->get_DriverManufacturer(value); });
        record.model = read_bstr([&](BSTR* value) { return driver->get_DriverModel(value); });
        record.provider = read_bstr([&](BSTR* value) { return driver->get_DriverProvider(value); });

        DATE driver_date = 0;
        if (SUCCEEDED(driver->get_DriverVerDate(&driver_date))) {
            record.driver_date_yyyymmdd = date_yyyymmdd(driver_date);
        }
        LONG number = 0;
        if (SUCCEEDED(driver->get_DeviceProblemNumber(&number))) {
            record.device_problem_number = number;
        }
        if (SUCCEEDED(driver->get_DeviceStatus(&number))) {
            record.device_status = number;
        }

        VARIANT_BOOL boolean_value = VARIANT_FALSE;
        if (SUCCEEDED(update->get_IsDownloaded(&boolean_value))) {
            record.downloaded = boolean_value == VARIANT_TRUE;
        }
        if (SUCCEEDED(update->get_IsHidden(&boolean_value))) {
            record.hidden = boolean_value == VARIANT_TRUE;
        }
        if (SUCCEEDED(update->get_IsMandatory(&boolean_value))) {
            record.mandatory = boolean_value == VARIANT_TRUE;
        }

        ComPtr<IUpdateIdentity> identity;
        if (SUCCEEDED(update->get_Identity(&identity))) {
            record.update_id = read_bstr(
                [&](BSTR* value) { return identity->get_UpdateID(value); });
            LONG revision = 0;
            if (SUCCEEDED(identity->get_RevisionNumber(&revision))) {
                record.revision_number = revision;
            }
        }
        if (record.update_id.empty() || record.driver_hardware_id.empty()) {
            result.fatal_error = WindowsUpdateError{
                .operation = "Windows Update driver metadata",
                .hresult = static_cast<std::int32_t>(E_INVALIDARG),
                .message = "a fonte retornou um driver sem identidade completa",
            };
            result.updates.clear();
            return result;
        }

        auto matches = matching_instances(inventory, record.driver_hardware_id);
        record.matching_instance_ids = std::move(matches.all);
        record.matching_present_instance_ids = std::move(matches.present);
        result.updates.push_back(std::move(record));
    }

    return result;
}

}  // namespace open_driver

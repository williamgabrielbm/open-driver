// Language: C++20; file: transaction_executor_cli.cpp; target: Windows 11; runtime: MSVC/CMake.
#include "candidate_ranker.hpp"
#include "device_inventory.hpp"
#include "external_package_source.hpp"
#include "microsoft_catalog.hpp"
#include "package_materializer.hpp"
#include "recommendation_engine.hpp"
#include "transaction_executor.hpp"
#include "update_plan.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

namespace {

std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::wstring wide_from_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
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

std::string windows_error(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (count == 0 || buffer == nullptr) return "Windows error " + std::to_string(code);
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, buffer, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, buffer, static_cast<int>(count), result.data(), required,
        nullptr, nullptr);
    LocalFree(buffer);
    while (!result.empty() &&
           (result.back() == '\r' || result.back() == '\n' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

bool process_is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(
        token, TokenElevation, &elevation, sizeof(elevation), &size) &&
        elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

std::optional<std::filesystem::path> trusted_executable(const std::string& executable) {
    const auto name = upper_ascii(std::filesystem::path(executable).filename().string());
    std::array<wchar_t, MAX_PATH> directory{};
    if (name == "PNPUTIL.EXE") {
        const auto count = GetSystemDirectoryW(directory.data(),
                                               static_cast<UINT>(directory.size()));
        if (count == 0 || count >= directory.size()) return std::nullopt;
        return std::filesystem::path(directory.data()) / L"pnputil.exe";
    }
    if (name == "POWERSHELL.EXE") {
        const auto count = GetWindowsDirectoryW(directory.data(),
                                                static_cast<UINT>(directory.size()));
        if (count == 0 || count >= directory.size()) return std::nullopt;
        return std::filesystem::path(directory.data()) /
               L"System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    }
    return std::nullopt;
}

open_driver::ProcessResult run_trusted_process(
    const open_driver::PlannedCommand& command) {
    open_driver::ProcessResult result;
    if (command.requires_elevation && !process_is_elevated()) {
        result.error = "administrator privileges are required for this command";
        return result;
    }
    auto effective_command = command;
    std::optional<std::filesystem::path> export_root;
    std::optional<std::filesystem::path> export_inf_name;
    if (command.id == "export-current-driver" && command.arguments.size() == 3) {
        export_root = std::filesystem::path(wide_from_utf8(command.arguments[2]));
        export_inf_name = std::filesystem::path(wide_from_utf8(command.arguments[1]));
        std::error_code error;
        if (std::filesystem::exists(*export_root, error)) {
            if (error || !std::filesystem::is_directory(*export_root, error) ||
                !std::filesystem::is_empty(*export_root, error)) {
                result.error = "rollback directory already exists and is not empty";
                return result;
            }
        } else if (!std::filesystem::create_directories(*export_root, error) || error) {
            result.error = "could not create the rollback directory: " + error.message();
            return result;
        }
    }
    if (command.id == "restore-exported-driver" && command.arguments.size() == 3) {
        const auto planned_inf = std::filesystem::path(
            wide_from_utf8(command.arguments[1]));
        if (!std::filesystem::is_regular_file(planned_inf)) {
            const auto found = open_driver::find_exported_driver_inf(
                planned_inf.parent_path(), planned_inf.filename());
            if (!found) {
                result.error = "the exported rollback INF could not be located uniquely";
                return result;
            }
            effective_command.arguments[1] = utf8_from_wide(found->wstring());
        }
    }
    const auto executable = trusted_executable(effective_command.executable);
    if (!executable || !std::filesystem::is_regular_file(*executable)) {
        result.error = "trusted system executable was not found";
        return result;
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        result.error = "CreatePipe: " + windows_error(GetLastError());
        return result;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        result.error = "SetHandleInformation: " + windows_error(GetLastError());
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    auto resolved = effective_command;
    resolved.executable = executable->string();
    auto command_line = wide_from_utf8(open_driver::planned_command_preview(resolved));
    if (command_line.empty()) {
        result.error = "could not encode the command line";
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }
    command_line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable->c_str(), command_line.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        result.error = "CreateProcessW: " + windows_error(GetLastError());
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }
    result.started = true;
    CloseHandle(write_pipe);

    std::thread reader([&] {
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                        &read, nullptr) && read != 0) {
            result.output.append(buffer.data(), read);
        }
    });
    constexpr DWORD timeout_ms = 5 * 60 * 1000;
    const auto wait = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(process.hProcess, 10000);
    } else if (wait == WAIT_FAILED) {
        result.error = "WaitForSingleObject: " + windows_error(GetLastError());
        TerminateProcess(process.hProcess, ERROR_GEN_FAILURE);
        WaitForSingleObject(process.hProcess, 10000);
    }
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        result.exit_code = exit_code;
        result.reboot_required = exit_code == ERROR_SUCCESS_REBOOT_REQUIRED;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    reader.join();
    CloseHandle(read_pipe);
    if (!result.timed_out && result.exit_code == 0 && export_root && export_inf_name &&
        !open_driver::find_exported_driver_inf(*export_root, *export_inf_name)) {
        result.exit_code = ERROR_FILE_NOT_FOUND;
        result.error = "pnputil reported success but the exported rollback INF was not found";
    }
    return result;
}

open_driver::PlanObservation observe(const open_driver::UpdatePlan& plan) {
    open_driver::PlanObservation observation;
    const auto inventory = open_driver::collect_device_inventory({.present_only = false});
    if (inventory.fatal_error) {
        observation.observation_error = inventory.fatal_error->operation + ": " +
                                        inventory.fatal_error->message;
        return observation;
    }
    const auto requested = upper_ascii(plan.device_instance_id);
    const auto device = std::find_if(inventory.devices.begin(), inventory.devices.end(),
        [&](const auto& value) { return upper_ascii(value.instance_id) == requested; });
    if (device == inventory.devices.end()) {
        observation.observation_error = "device instance was not found";
        return observation;
    }
    observation.device_found = true;
    observation.device_present = device->present;
    observation.device_instance_id = device->instance_id;
    observation.installed_driver = device->installed_driver;
    observation.cab_sha256_base64 = open_driver::sha256_file_base64(plan.package.cab_path);
    if (observation.cab_sha256_base64.empty()) {
        observation.observation_error = "could not hash the CAB from the plan";
        return observation;
    }
    std::string catalog_status;
    observation.catalog_signature_valid = open_driver::verify_catalog_signature_file(
        plan.package.catalog_path, catalog_status);

    auto discovery = open_driver::analyze_external_inf_for_device(
        *device, plan.candidate.inf_path);
    if (discovery.fatal_error) {
        observation.observation_error = discovery.fatal_error->operation + ": " +
                                        discovery.fatal_error->message;
        return observation;
    }
    auto ranking = open_driver::rank_candidates(std::move(discovery.candidates));
    if (ranking.accepted.empty()) {
        observation.observation_error = "the planned INF has no accepted current candidate";
        return observation;
    }
    const auto& candidate = ranking.accepted.front().candidate;
    observation.inf_sha256_hex = candidate.package_hash;
    observation.matching_hardware_id = candidate.matching_id;
    observation.windows_rank = candidate.windows_rank;
    const open_driver::RecommendationContext context{
        .catalog_resolution_unambiguous = true,
        .package_hash_verified = observation.cab_sha256_base64 ==
                                 plan.package.cab_sha256_base64,
        .catalog_signature_valid = observation.catalog_signature_valid,
    };
    observation.recommendation = open_driver::recommend_driver_action(
        *device, candidate, context).decision;
    return observation;
}

}  // namespace

int main(int argc, char** argv) {
    std::string plan_path;
    std::string journal_path;
    std::string confirmation;
    bool apply = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--plan" && index + 1 < argc) {
            plan_path = argv[++index];
        } else if (argument == "--journal" && index + 1 < argc) {
            journal_path = argv[++index];
        } else if (argument == "--apply") {
            apply = true;
        } else if (argument == "--confirm" && index + 1 < argc) {
            confirmation = argv[++index];
        } else {
            std::cerr << "usage: open_driver_transaction --plan <path> --journal <new-path> "
                         "[--apply --confirm <plan-id>]\n";
            return 2;
        }
    }
    if (plan_path.empty() || journal_path.empty()) {
        std::cerr << "usage: open_driver_transaction --plan <path> --journal <new-path> "
                     "[--apply --confirm <plan-id>]\n";
        return 2;
    }
    if (std::filesystem::exists(journal_path)) {
        std::cerr << "journal path already exists: " << journal_path << '\n';
        return 4;
    }

    std::ifstream input(plan_path, std::ios::binary);
    if (!input) {
        std::cerr << "could not open plan: " << plan_path << '\n';
        return 1;
    }
    const std::string json(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto parsed = open_driver::parse_update_plan_json(json);
    if (!parsed.plan) {
        std::cerr << "invalid plan: " << parsed.error << '\n';
        return 3;
    }
    if (apply && confirmation != parsed.plan->plan_id) {
        std::cerr << "apply confirmation must exactly match plan_id="
                  << parsed.plan->plan_id << '\n';
        return 5;
    }

    open_driver::PlanObservation observation;
    if (parsed.plan->ready && parsed.plan->blockers.empty()) {
        observation = observe(*parsed.plan);
    }
    std::ofstream output(journal_path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "could not create journal: " << journal_path << '\n';
        return 1;
    }
    open_driver::ExecutionJournal journal;
    if (apply) {
        journal = open_driver::execute_update_plan_apply(
            *parsed.plan, observation, run_trusted_process,
            [&] { return observe(*parsed.plan); },
            [&](const auto& current, const auto& event) {
                output << open_driver::execution_event_json_line(current, event);
                output.flush();
                return static_cast<bool>(output);
            });
    } else {
        journal = open_driver::execute_update_plan_dry_run(*parsed.plan, observation);
        for (const auto& event : journal.events) {
            output << open_driver::execution_event_json_line(journal, event);
            output.flush();
            if (!output) {
                std::cerr << "could not append journal event " << event.sequence << '\n';
                return 1;
            }
        }
    }
    std::cout << "plan_id=" << journal.plan_id
              << " mode=" << journal.mode
              << " completed=" << (journal.completed ? "true" : "false")
              << " blocked=" << (journal.blocked ? "true" : "false")
              << " rollback_attempted="
              << (journal.rollback_attempted ? "true" : "false")
              << " rollback_completed="
              << (journal.rollback_completed ? "true" : "false")
              << " reboot_required="
              << (journal.reboot_required ? "true" : "false")
              << " events=" << journal.events.size()
              << " journal=" << journal_path << '\n';
    if (apply && !journal.completed) return 6;
    return journal.reboot_required ? ERROR_SUCCESS_REBOOT_REQUIRED : 0;
}

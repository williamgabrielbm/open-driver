// Language: C++20; file: gui_main.cpp; target: Windows 10/11; runtime: MSVC/CMake.
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>

#include "device_inventory.hpp"
#include "update_orchestrator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int action_button_id = 101;
constexpr int list_id = 102;
constexpr int scan_button_id = 103;
constexpr UINT scan_complete_message = WM_APP + 1;
constexpr UINT status_message = WM_APP + 2;
constexpr UINT update_complete_message = WM_APP + 3;
constexpr UINT scan_progress_message = WM_APP + 4;
constexpr int sidebar_width = 148;
constexpr COLORREF background_color = RGB(30, 31, 34);
constexpr COLORREF sidebar_color = RGB(18, 19, 22);
constexpr COLORREF panel_color = RGB(39, 41, 45);
constexpr COLORREF panel_border_color = RGB(57, 60, 66);
constexpr COLORREF row_color = RGB(45, 47, 52);
constexpr COLORREF row_alt_color = RGB(41, 43, 48);
constexpr COLORREF row_selected_color = RGB(51, 66, 86);
constexpr COLORREF text_color = RGB(244, 246, 249);
constexpr COLORREF muted_color = RGB(166, 171, 181);
constexpr COLORREF subtle_color = RGB(112, 118, 130);
constexpr COLORREF accent_color = RGB(112, 177, 255);
constexpr COLORREF accent_hover_color = RGB(130, 188, 255);
constexpr COLORREF accent_pressed_color = RGB(82, 151, 235);
constexpr COLORREF accent_soft_color = RGB(37, 57, 82);

enum class ViewMode {
    home,
    scanning,
    results,
};

struct AppState {
    HWND window{};
    HWND title{};
    HWND summary{};
    HWND status{};
    HWND list{};
    HWND empty_state{};
    HWND action_button{};
    HWND scan_button{};
    HFONT brand_font{};
    HFONT brand_small_font{};
    HFONT title_font{};
    HFONT heading_font{};
    HFONT body_font{};
    HFONT small_font{};
    HFONT nav_font{};
    HFONT button_font{};
    HFONT ring_font{};
    HFONT ring_small_font{};
    HBRUSH background_brush{};
    HBRUSH sidebar_brush{};
    HBRUSH panel_brush{};
    HIMAGELIST row_height_image{};
    bool busy{};
    bool empty_success{};
    bool restart_pending{};
    ViewMode view{ViewMode::home};
    std::size_t scan_current{};
    std::size_t scan_total{};
    std::wstring scan_device;
    std::shared_ptr<std::atomic_bool> scan_cancel;
    std::vector<open_driver::UpdateOffer> offers;
};

struct ScanPayload {
    open_driver::UpdateDiscoveryReport report;
    std::string error;
    bool download_declined{};
    std::size_t download_candidate_count{};
    bool cancelled{};
};

struct ScanProgressPayload {
    std::size_t current{};
    std::size_t total{};
    std::wstring device;
};

struct UpdatePayload {
    bool success{};
    bool reboot_required{};
    std::wstring message;
};

std::wstring wide_from_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return L"Texto inválido";
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::wstring display_name(const open_driver::DeviceRecord& device) {
    if (!device.friendly_name.empty()) return wide_from_utf8(device.friendly_name);
    if (!device.description.empty()) return wide_from_utf8(device.description);
    return wide_from_utf8(device.installed_driver.description);
}

std::wstring friendly_date(std::uint32_t date) {
    if (date == 0) return L"—";
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02u/%02u/%04u", date % 100U, (date / 100U) % 100U,
               date / 10000U);
    return buffer;
}

std::wstring friendly_size(std::uint64_t bytes) {
    wchar_t buffer[64]{};
    if (bytes >= 1'000'000'000ULL) {
        swprintf_s(buffer, L"%.2f GB", static_cast<double>(bytes) / 1'000'000'000.0);
    } else if (bytes >= 1'000'000ULL) {
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / 1'000'000.0);
    } else if (bytes >= 1'000ULL) {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1'000.0);
    } else {
        swprintf_s(buffer, L"%llu bytes", static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

std::filesystem::path application_data_root() {
    std::array<wchar_t, 32768> value{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(),
                                                 static_cast<DWORD>(value.size()));
    if (count == 0 || count >= value.size()) return L"OpenDriverData";
    return std::filesystem::path(value.data()) / L"OpenDriver";
}

std::filesystem::path restart_marker_path() {
    return application_data_root() / L"restart-required";
}

std::uint64_t current_filetime() {
    FILETIME value{};
    GetSystemTimeAsFileTime(&value);
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

void remember_restart_required() {
    std::error_code error;
    std::filesystem::create_directories(application_data_root(), error);
    std::ofstream output(restart_marker_path(), std::ios::binary | std::ios::trunc);
    if (output) output << current_filetime();
}

bool restart_is_pending() {
    std::ifstream input(restart_marker_path(), std::ios::binary);
    std::uint64_t requested_at = 0;
    if (!(input >> requested_at)) return false;
    const auto booted_at = current_filetime() - GetTickCount64() * 10000ULL;
    if (requested_at >= booted_at) return true;
    input.close();
    std::error_code error;
    std::filesystem::remove(restart_marker_path(), error);
    return false;
}

std::filesystem::path executable_directory() {
    std::array<wchar_t, 32768> value{};
    const DWORD count = GetModuleFileNameW(nullptr, value.data(),
                                          static_cast<DWORD>(value.size()));
    if (count == 0 || count >= value.size()) return {};
    return std::filesystem::path(value.data()).parent_path();
}

std::wstring quote(const std::filesystem::path& value) {
    return L"\"" + value.wstring() + L"\"";
}

std::string json_string_field(const std::string& line, const std::string& name) {
    const auto marker = "\"" + name + "\":\"";
    auto cursor = line.find(marker);
    if (cursor == std::string::npos) return {};
    cursor += marker.size();
    std::string value;
    bool escaped = false;
    for (; cursor < line.size(); ++cursor) {
        const char character = line[cursor];
        if (escaped) {
            switch (character) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(character); break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            break;
        } else {
            value.push_back(character);
        }
    }
    return value;
}

std::wstring journal_failure_message(const std::filesystem::path& journal_path) {
    std::ifstream input(journal_path, std::ios::binary);
    std::string line;
    std::string last_step;
    std::string last_message;
    while (std::getline(input, line)) {
        const auto status = json_string_field(line, "status");
        if (status != "blocked" && status != "failed") continue;
        last_step = json_string_field(line, "step_id");
        last_message = json_string_field(line, "message");
    }
    if (last_step == "match-hardware-id") {
        return L"A identidade do dispositivo mudou entre a verificação e a instalação.";
    }
    if (last_step == "match-installed-snapshot") {
        return L"O driver instalado mudou depois da verificação. Faça uma nova busca.";
    }
    if (last_step == "verify-cab-hash" || last_step == "verify-inf-hash") {
        return L"O pacote baixado mudou ou foi corrompido durante a validação.";
    }
    if (last_step == "verify-catalog-signature") {
        return L"A assinatura digital do pacote deixou de ser válida.";
    }
    if (last_step == "recompute-recommendation") {
        return L"A verificação final concluiu que o driver não deve substituir o atual.";
    }
    if (!last_message.empty()) {
        return L"Etapa " + wide_from_utf8(last_step) + L": " +
               wide_from_utf8(last_message);
    }
    return {};
}

void post_status(HWND window, std::wstring message) {
    auto* text = new std::wstring(std::move(message));
    if (!PostMessageW(window, status_message, 0, reinterpret_cast<LPARAM>(text))) delete text;
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

bool run_prepared_update(
    const open_driver::PreparedUpdate& update,
    bool& reboot_required,
    std::wstring& error) {
    reboot_required = false;
    const auto executable = executable_directory() / L"open_driver_transaction.exe";
    if (!std::filesystem::is_regular_file(executable)) {
        error = L"O executor de instalação não foi encontrado ao lado do aplicativo.";
        return false;
    }
    const auto parameters = L"--plan " + quote(update.plan_path) +
        L" --journal " + quote(update.journal_path) + L" --apply --confirm \"" +
        wide_from_utf8(update.plan_id) + L"\"";
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    // The GUI normally starts elevated through its manifest. In that case the
    // child inherits the token and Windows does not show another UAC prompt.
    launch.lpVerb = process_is_elevated() ? L"open" : L"runas";
    launch.lpFile = executable.c_str();
    launch.lpParameters = parameters.c_str();
    const auto directory = executable_directory();
    launch.lpDirectory = directory.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch)) {
        const auto code = GetLastError();
        error = code == ERROR_CANCELLED
            ? L"A permissão de administrador foi cancelada."
            : L"O Windows não conseguiu iniciar a instalação (erro " +
                  std::to_wstring(code) + L").";
        return false;
    }
    WaitForSingleObject(launch.hProcess, INFINITE);
    DWORD exit_code = ERROR_GEN_FAILURE;
    GetExitCodeProcess(launch.hProcess, &exit_code);
    CloseHandle(launch.hProcess);
    reboot_required = exit_code == ERROR_SUCCESS_REBOOT_REQUIRED;
    if (exit_code != 0 && !reboot_required) {
        const auto detail = journal_failure_message(update.journal_path);
        error = detail.empty()
            ? L"A atualização foi interrompida. O motivo técnico foi salvo no diário do aplicativo."
            : L"A atualização foi interrompida: " + detail;
        return false;
    }
    return true;
}

void request_restart(HWND window) {
    const auto answer = MessageBoxW(window,
        L"O computador será reiniciado agora para concluir as atualizações de drivers.\n\n"
        L"Salve os arquivos abertos antes de continuar.",
        L"Reiniciar o computador", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES) return;
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        window, L"open", L"shutdown.exe", L"/r /t 0", nullptr, SW_HIDE));
    if (result <= 32) {
        MessageBoxW(window,
            L"O Windows não iniciou a reinicialização. Reinicie pelo menu Iniciar.",
            L"Open Driver", MB_OK | MB_ICONERROR);
    }
}

void set_list_text(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

void layout(AppState& state, int width, int height);

void set_view(AppState& state, ViewMode view) {
    state.view = view;
    const bool results = view == ViewMode::results;
    for (const auto control : {state.title, state.summary, state.status,
                               state.action_button}) {
        ShowWindow(control, results ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(state.scan_button, results ? SW_HIDE : SW_SHOW);
    if (!results) {
        ShowWindow(state.list, SW_HIDE);
        ShowWindow(state.empty_state, SW_HIDE);
    }
    RECT area{};
    GetClientRect(state.window, &area);
    layout(state, area.right, area.bottom);
    InvalidateRect(state.window, nullptr, TRUE);
}

void post_scan_progress(
    HWND window, std::size_t current, std::size_t total, std::wstring device) {
    auto* progress = new ScanProgressPayload{
        .current = current,
        .total = total,
        .device = std::move(device),
    };
    if (!PostMessageW(window, scan_progress_message, 0,
                      reinterpret_cast<LPARAM>(progress))) {
        delete progress;
    }
}

void show_offers(AppState& state, ScanPayload& payload) {
    set_view(state, ViewMode::results);
    ListView_DeleteAllItems(state.list);
    state.offers.clear();
    if (!payload.error.empty()) {
        state.empty_success = false;
        ShowWindow(state.list, SW_HIDE);
        ShowWindow(state.empty_state, SW_SHOW);
        SetWindowTextW(state.empty_state,
            L"Não foi possível terminar a verificação\nUse o botão acima para tentar novamente.");
        SetWindowTextW(state.summary, L"Não foi possível concluir a verificação");
        SetWindowTextW(state.status, wide_from_utf8(payload.error).c_str());
        SetWindowTextW(state.action_button, L"Tentar novamente");
        return;
    }
    if (!payload.report.complete) {
        state.empty_success = false;
        ShowWindow(state.list, SW_HIDE);
        ShowWindow(state.empty_state, SW_SHOW);
        SetWindowTextW(state.empty_state,
            L"A verificação ficou incompleta\nNenhum resultado parcial foi exibido.");
        SetWindowTextW(state.summary, L"Verificação não concluída");
        const auto status = L"Nenhum resultado foi exibido. " +
            std::to_wstring(payload.report.source_checks_completed) + L" de " +
            std::to_wstring(payload.report.source_checks_required) +
            L" verificações obrigatórias terminaram. O aplicativo tentou cada falha 3 vezes.";
        SetWindowTextW(state.status, status.c_str());
        SetWindowTextW(state.action_button, L"Tentar novamente");
        return;
    }
    if (payload.download_declined) {
        state.empty_success = false;
        ShowWindow(state.list, SW_HIDE);
        ShowWindow(state.empty_state, SW_SHOW);
        SetWindowTextW(state.empty_state,
            L"Download não autorizado\nNenhum pacote foi baixado ou instalado.");
        SetWindowTextW(state.summary,
            (std::to_wstring(payload.download_candidate_count) +
             (payload.download_candidate_count == 1
                 ? L" pacote candidato encontrado"
                 : L" pacotes candidatos encontrados")).c_str());
        SetWindowTextW(state.status,
            L"O download não foi autorizado. Nenhum pacote de driver foi baixado ou instalado.");
        SetWindowTextW(state.action_button, L"Verificar novamente");
        return;
    }
    state.offers = std::move(payload.report.offers);
    const auto package_count = state.offers.size();
    const auto device_count = std::accumulate(state.offers.begin(), state.offers.end(),
        std::size_t{0}, [](std::size_t total, const auto& offer) {
            return total + offer.related_devices;
        });
    const std::wstring summary = device_count == 0
        ? (payload.report.candidates_rejected == 0
            ? L"Seus drivers compatíveis estão atualizados"
            : L"Atualizações encontradas, mas recusadas pela validação final")
        : std::to_wstring(device_count) +
          (device_count == 1 ? L" dispositivo desatualizado" : L" dispositivos desatualizados") +
          (package_count == device_count ? L"" : L" em " + std::to_wstring(package_count) + L" pacotes");
    SetWindowTextW(state.summary, summary.c_str());
    if (device_count == 0) {
        state.empty_success = payload.report.candidates_rejected == 0;
        ShowWindow(state.list, SW_HIDE);
        ShowWindow(state.empty_state, SW_SHOW);
        SetWindowTextW(state.empty_state,
            L"Tudo certo por aqui\nSeus drivers compatíveis estão atualizados.");
        std::wstring status = payload.report.candidates_rejected == 0
            ? L"Verificação 100% concluída: todo o hardware presente e todas as fontes obrigatórias responderam."
            : std::to_wstring(payload.report.candidates_rejected) +
              (payload.report.candidates_rejected == 1
                  ? L" atualização foi recusada porque o pacote não passou na conferência final."
                  : L" atualizações foram recusadas porque os pacotes não passaram na conferência final.");
        if (!payload.report.rejections.empty()) {
            status += L" Motivo: " + wide_from_utf8(payload.report.rejections.front());
        }
        SetWindowTextW(state.status, status.c_str());
        SetWindowTextW(state.action_button, L"Verificar novamente");
    } else {
        state.empty_success = false;
        ShowWindow(state.empty_state, SW_HIDE);
        ShowWindow(state.list, SW_SHOW);
        std::wstring status = std::to_wstring(payload.report.devices_detected) +
            L" dispositivos detectados; " + std::to_wstring(payload.report.devices_checked) +
            L" componentes físicos verificados em " +
            std::to_wstring(payload.report.packages_checked) +
            L" identidades de hardware. Verificação 100% concluída; só atualizações compatíveis aparecem abaixo.";
        if (payload.report.candidates_rejected != 0) {
            status += L" " + std::to_wstring(payload.report.candidates_rejected) +
                (payload.report.candidates_rejected == 1
                    ? L" candidato foi descartado pela validação final."
                    : L" candidatos foram descartados pela validação final.");
        }
        SetWindowTextW(state.status, status.c_str());
        SetWindowTextW(state.action_button, L"Atualizar selecionados");
    }
    for (std::size_t index = 0; index < state.offers.size(); ++index) {
        const auto& offer = state.offers[index];
        auto name = display_name(offer.device);
        if (offer.related_devices > 1) {
            name += L"  (" + std::to_wstring(offer.related_devices) + L" dispositivos)";
        }
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = name.data();
        item.lParam = static_cast<LPARAM>(index);
        const int row = ListView_InsertItem(state.list, &item);
        set_list_text(state.list, row, 1, wide_from_utf8(offer.device.installed_driver.version));
        set_list_text(state.list, row, 2, wide_from_utf8(offer.available_version));
        set_list_text(state.list, row, 3, friendly_date(offer.available_date_yyyymmdd));
        set_list_text(state.list, row, 4, wide_from_utf8(
            offer.source_name.empty() ? "Microsoft Update Catalog" : offer.source_name));
        set_list_text(state.list, row, 5, L"Atualizar");
        ListView_SetCheckState(state.list, row, TRUE);
    }
}

void start_scan(AppState& state) {
    if (state.busy) return;
    state.restart_pending = false;
    state.busy = true;
    state.empty_success = false;
    state.scan_current = 0;
    state.scan_total = 0;
    state.scan_device = L"Preparando a leitura do computador...";
    state.scan_cancel = std::make_shared<std::atomic_bool>(false);
    state.offers.clear();
    ListView_DeleteAllItems(state.list);
    set_view(state, ViewMode::scanning);
    EnableWindow(state.scan_button, TRUE);
    EnableWindow(state.action_button, FALSE);
    SetWindowTextW(state.action_button, L"Verificando...");
    SetWindowTextW(state.summary, L"Procurando drivers compatíveis");
    SetWindowTextW(state.status,
        L"O aplicativo está lendo o hardware e consultando as fontes de drivers.");
    std::thread([window = state.window, cancel = state.scan_cancel] {
        auto payload = std::make_unique<ScanPayload>();
        try {
        const auto inventory = open_driver::collect_device_inventory({.present_only = true});
        if (cancel->load()) {
            payload->cancelled = true;
        } else if (inventory.fatal_error) {
            payload->error = inventory.fatal_error->operation + ": " +
                             inventory.fatal_error->message;
        } else {
            payload->report = open_driver::discover_updates(inventory.devices,
                [window](std::size_t current, std::size_t total, const std::string& name) {
                    post_scan_progress(window, current, total, wide_from_utf8(name));
                });
            if (cancel->load()) {
                payload->cancelled = true;
                payload->report.offers.clear();
            } else if (!payload->report.complete) {
                payload->error = "A verificação não terminou por inteiro. Nenhuma lista parcial será exibida. " +
                    std::to_string(payload->report.source_checks_completed) + " de " +
                    std::to_string(payload->report.source_checks_required) +
                    " verificações obrigatórias responderam.";
                if (!payload->report.errors.empty()) {
                    payload->error += " Motivo: " + payload->report.errors.front();
                }
            } else if (!payload->report.offers.empty()) {
                std::set<std::string> unique_packages;
                std::uint64_t estimated_bytes = 0;
                for (const auto& offer : payload->report.offers) {
                    const auto key = !offer.source_package_id.empty()
                        ? offer.source_package_id : offer.search.catalog_update_id;
                    if (unique_packages.insert(key).second) {
                        estimated_bytes += offer.estimated_download_bytes;
                    }
                }
                payload->download_candidate_count = unique_packages.size();
                std::wstring question = L"Foram encontrados " +
                    std::to_wstring(payload->download_candidate_count) +
                    (payload->download_candidate_count == 1
                        ? L" pacote candidato."
                        : L" pacotes candidatos.");
                if (estimated_bytes != 0) {
                    question += L"\nTamanho estimado: " + friendly_size(estimated_bytes) + L".";
                }
                question +=
                    L"\n\nPara confirmar quais drivers realmente são compatíveis, o Open Driver "
                    L"precisa baixar e validar esses pacotes.\n\nPermitir o download agora?";
                if (!cancel->load() &&
                    MessageBoxW(window, question.c_str(), L"Autorizar download de drivers",
                                MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES) {
                    open_driver::validate_and_prepare_offers(
                        payload->report, application_data_root(),
                        [window](std::size_t current, std::size_t total,
                                 const std::string& name) {
                            post_scan_progress(window, current, total,
                                L"Validando pacote: " + wide_from_utf8(name));
                        });
                    if (cancel->load()) {
                        payload->cancelled = true;
                        payload->report.offers.clear();
                    } else if (!payload->report.complete) {
                        payload->error =
                            "A validação dos pacotes não terminou por inteiro. Nenhuma lista parcial será exibida.";
                        if (!payload->report.errors.empty()) {
                            payload->error += " Motivo: " + payload->report.errors.front();
                        }
                    }
                } else if (cancel->load()) {
                    payload->cancelled = true;
                    payload->report.offers.clear();
                } else {
                    payload->download_declined = true;
                    payload->report.offers.clear();
                }
            }
        }
        } catch (const std::exception& error) {
            payload->report.complete = false;
            payload->report.offers.clear();
            payload->error = std::string("A verificação encontrou uma falha interna: ") + error.what();
        } catch (...) {
            payload->report.complete = false;
            payload->report.offers.clear();
            payload->error = "A verificação encontrou uma falha interna desconhecida.";
        }
        auto* raw = payload.release();
        if (!PostMessageW(window, scan_complete_message, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
    }).detach();
}

void start_updates(AppState& state, std::vector<std::size_t> indices) {
    if (state.busy || indices.empty()) return;
    auto question = L"O Open Driver vai baixar, validar e instalar " +
        std::to_wstring(indices.size()) + (indices.size() == 1 ? L" driver." : L" drivers.") +
        L"\n\n";
    question += process_is_elevated()
        ? L"A permissão de administrador já está ativa nesta janela. Continuar?"
        : L"O Windows pedirá permissão de administrador. Continuar?";
    if (MessageBoxW(state.window, question.c_str(), L"Confirmar atualização",
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
    state.busy = true;
    EnableWindow(state.action_button, FALSE);
    SetWindowTextW(state.action_button, L"Atualizando...");
    std::vector<open_driver::UpdateOffer> offers;
    for (const auto index : indices) if (index < state.offers.size()) offers.push_back(state.offers[index]);
    std::thread([window = state.window, offers = std::move(offers)] {
        auto payload = std::make_unique<UpdatePayload>();
        try {
        payload->success = true;
        bool reboot_required = false;
        std::size_t installed_count = 0;
        std::size_t skipped_count = 0;
        for (std::size_t index = 0; index < offers.size(); ++index) {
            const auto inventory = open_driver::collect_device_inventory({.present_only = true});
            if (inventory.fatal_error) {
                payload->success = false;
                payload->message = L"Não foi possível confirmar o estado atual do dispositivo: " +
                    wide_from_utf8(inventory.fatal_error->message);
                break;
            }
            const auto current = std::find_if(inventory.devices.begin(), inventory.devices.end(),
                [&](const auto& device) {
                    return _stricmp(device.instance_id.c_str(),
                                    offers[index].device.instance_id.c_str()) == 0;
                });
            if (current == inventory.devices.end() ||
                !open_driver::device_snapshot_matches_offer(offers[index], *current)) {
                ++skipped_count;
                post_status(window, L"Pulando uma linha que já mudou; a lista será verificada novamente.");
                continue;
            }
            post_status(window, L"Baixando e validando " + std::to_wstring(index + 1) + L" de " +
                std::to_wstring(offers.size()) + L": " + display_name(offers[index].device));
            auto prepared = offers[index].prepared
                ? open_driver::PrepareUpdateResult{.prepared = offers[index].prepared}
                : open_driver::prepare_update(offers[index], application_data_root());
            if (!prepared.prepared) {
                payload->success = false;
                payload->message = L"Não foi possível preparar " + display_name(offers[index].device) +
                                   L": " + wide_from_utf8(prepared.error);
                break;
            }
            post_status(window, L"Instalando " + display_name(offers[index].device) + L"...");
            std::wstring error;
            bool package_reboot_required = false;
            if (!run_prepared_update(*prepared.prepared, package_reboot_required, error)) {
                payload->success = false;
                payload->message = error;
                break;
            }
            reboot_required = reboot_required || package_reboot_required;
            ++installed_count;
        }
        payload->reboot_required = reboot_required;
        if (payload->success) {
            payload->message = installed_count == 0
                ? L"A lista mudou desde a verificação. Verificando novamente..."
                : std::to_wstring(installed_count) +
                    (installed_count == 1 ? L" pacote instalado" : L" pacotes instalados") +
                    (skipped_count == 0 ? L"." : L"; linhas já alteradas foram ignoradas.") +
                    (reboot_required
                        ? L" Todas as instalações terminaram. Reinicialização necessária."
                        : L" Verificando os drivers novamente...");
        }
        } catch (const std::exception& error) {
            payload->success = false;
            payload->message = L"A atualização encontrou uma falha interna: " +
                               wide_from_utf8(error.what());
        } catch (...) {
            payload->success = false;
            payload->message = L"A atualização encontrou uma falha interna desconhecida.";
        }
        auto* raw = payload.release();
        if (!PostMessageW(window, update_complete_message, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
    }).detach();
}

void layout(AppState& state, int width, int height) {
    constexpr int margin = 24;
    const int content_x = sidebar_width + margin;
    const int content_width = std::max(560, width - content_x - margin);
    MoveWindow(state.title, content_x, 24, content_width - 220, 24, TRUE);
    MoveWindow(state.summary, content_x, 52, content_width - 220, 42, TRUE);
    MoveWindow(state.action_button, width - margin - 194, 38, 194, 46, TRUE);
    MoveWindow(state.status, content_x + 18, 120, content_width - 36, 52, TRUE);
    const int list_top = 242;
    const int list_height = std::max(190, height - list_top - 24);
    MoveWindow(state.list, content_x, list_top, content_width, list_height, TRUE);
    MoveWindow(state.empty_state, content_x + 32,
               list_top + list_height / 2 + 2, content_width - 64, 62, TRUE);

    constexpr int scan_size = 250;
    const int main_width = width - sidebar_width;
    const int center_shift = state.view == ViewMode::home ? -76 : 0;
    const int scan_left = sidebar_width + main_width / 2 - scan_size / 2 + center_shift;
    const int scan_top = std::max(150, height / 2 - scan_size / 2 + 18);
    MoveWindow(state.scan_button, scan_left, scan_top, scan_size, scan_size, TRUE);

    const int driver_width = std::max(250, content_width * 31 / 100);
    const int current_width = std::max(112, content_width * 13 / 100);
    const int available_width = std::max(112, content_width * 13 / 100);
    const int date_width = std::max(98, content_width * 11 / 100);
    const int action_width = 102;
    const int source_width = std::max(120,
        content_width - driver_width - current_width - available_width - date_width - action_width - 8);
    const std::array<int, 6> widths{
        driver_width, current_width, available_width, date_width, source_width, action_width};
    for (int index = 0; index < static_cast<int>(widths.size()); ++index) {
        ListView_SetColumnWidth(state.list, index, widths[index]);
    }
}

void fill_rounded_rect(HDC dc, const RECT& area, int radius, COLORREF color) {
    auto region = CreateRoundRectRgn(
        area.left, area.top, area.right + 1, area.bottom + 1, radius, radius);
    auto brush = CreateSolidBrush(color);
    FillRgn(dc, region, brush);
    DeleteObject(brush);
    DeleteObject(region);
}

void frame_rounded_rect(HDC dc, const RECT& area, int radius, COLORREF color) {
    auto pen = CreatePen(PS_SOLID, 1, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, area.left, area.top, area.right, area.bottom, radius, radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_text(HDC dc, HFONT font, COLORREF color, const wchar_t* text,
               RECT area, UINT format) {
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &area, format);
    SelectObject(dc, old_font);
}

void draw_update_icon(HDC dc, int center_x, int center_y, COLORREF color) {
    auto pen = CreatePen(PS_SOLID, 2, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, center_x - 10, center_y - 10, center_x + 10, center_y + 10,
        center_x + 9, center_y - 4, center_x - 7, center_y - 7);
    MoveToEx(dc, center_x + 8, center_y - 7, nullptr);
    LineTo(dc, center_x + 9, center_y - 1);
    LineTo(dc, center_x + 3, center_y - 3);
    Arc(dc, center_x - 10, center_y - 10, center_x + 10, center_y + 10,
        center_x - 9, center_y + 4, center_x + 7, center_y + 7);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_tools_icon(HDC dc, int center_x, int center_y, COLORREF color) {
    auto pen = CreatePen(PS_SOLID, 2, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, center_x - 10, center_y - 7, center_x + 10, center_y + 8, 3, 3);
    MoveToEx(dc, center_x - 5, center_y - 11, nullptr);
    LineTo(dc, center_x + 5, center_y - 11);
    LineTo(dc, center_x + 5, center_y - 7);
    MoveToEx(dc, center_x - 5, center_y - 11, nullptr);
    LineTo(dc, center_x - 5, center_y - 7);
    MoveToEx(dc, center_x - 5, center_y, nullptr);
    LineTo(dc, center_x + 5, center_y);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_history_icon(HDC dc, int center_x, int center_y, COLORREF color) {
    auto pen = CreatePen(PS_SOLID, 2, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, center_x - 10, center_y - 10, center_x + 10, center_y + 10);
    MoveToEx(dc, center_x, center_y - 6, nullptr);
    LineTo(dc, center_x, center_y + 1);
    LineTo(dc, center_x + 5, center_y + 4);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_optimize_icon(HDC dc, int center_x, int center_y, COLORREF color) {
    auto pen = CreatePen(PS_SOLID, 2, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, center_x - 11, center_y - 8, center_x + 11, center_y + 14,
        center_x - 10, center_y + 7, center_x + 10, center_y + 7);
    MoveToEx(dc, center_x, center_y + 3, nullptr);
    LineTo(dc, center_x + 7, center_y - 4);
    Ellipse(dc, center_x - 2, center_y + 1, center_x + 3, center_y + 6);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_empty_icon(HDC dc, int center_x, int center_y, bool busy, bool success) {
    auto pen = CreatePen(PS_SOLID, 2, success ? accent_color : muted_color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, center_x - 18, center_y - 18, center_x + 18, center_y + 18);
    if (busy) {
        Arc(dc, center_x - 10, center_y - 10, center_x + 10, center_y + 10,
            center_x + 9, center_y - 4, center_x - 7, center_y - 7);
    } else if (success) {
        MoveToEx(dc, center_x - 8, center_y, nullptr);
        LineTo(dc, center_x - 2, center_y + 6);
        LineTo(dc, center_x + 10, center_y - 8);
    } else {
        MoveToEx(dc, center_x, center_y - 9, nullptr);
        LineTo(dc, center_x, center_y + 4);
        Ellipse(dc, center_x - 1, center_y + 9, center_x + 2, center_y + 12);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_sidebar_item(AppState& state, HDC dc, int top, const wchar_t* label,
                       bool selected, int icon) {
    RECT item{10, top, sidebar_width - 10, top + 72};
    if (selected) fill_rounded_rect(dc, item, 10, accent_soft_color);
    const auto color = selected ? accent_color : subtle_color;
    const int icon_x = sidebar_width / 2;
    const int icon_y = top + 23;
    if (icon == 0) draw_update_icon(dc, icon_x, icon_y, color);
    else if (icon == 1) draw_optimize_icon(dc, icon_x, icon_y, color);
    else if (icon == 2) draw_tools_icon(dc, icon_x, icon_y, color);
    else draw_history_icon(dc, icon_x, icon_y, color);
    RECT text_area{item.left + 4, top + 42, item.right - 4, top + 66};
    draw_text(dc, state.nav_font, selected ? text_color : muted_color, label,
              text_area, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void paint_shell(AppState& state, HDC dc, const RECT& client) {
    FillRect(dc, &client, state.background_brush);
    RECT sidebar{0, 0, sidebar_width, client.bottom};
    FillRect(dc, &sidebar, state.sidebar_brush);

    RECT brand{18, 22, sidebar_width - 18, 48};
    draw_text(dc, state.brand_font, text_color, L"OPEN", brand,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT brand_secondary{18, 48, sidebar_width - 18, 70};
    draw_text(dc, state.brand_small_font, accent_color, L"DRIVER", brand_secondary,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const auto first_label = state.view == ViewMode::results ? L"Atualizar" : L"Verificar";
    draw_sidebar_item(state, dc, 98, first_label, true, 0);
    draw_sidebar_item(state, dc, 178, L"Otimizar", false, 1);
    draw_sidebar_item(state, dc, 258, L"Ferramentas", false, 2);
    draw_sidebar_item(state, dc, 338, L"Histórico", false, 3);

    RECT version_area{18, client.bottom - 54, sidebar_width - 18, client.bottom - 24};
    draw_text(dc, state.small_font, subtle_color, L"Open Driver 1.0", version_area,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const int content_x = sidebar_width + 24;
    const int content_right = client.right - 24;
    if (state.view == ViewMode::home) {
        RECT app_title{content_x, 24, content_right, 54};
        draw_text(dc, state.title_font, muted_color, L"Open Driver", app_title,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        RECT instruction{content_x, 96, content_right - 20, 132};
        draw_text(dc, state.body_font, text_color,
                  L"Verifique todos os componentes do computador",
                  instruction, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        const int card_width = 258;
        const int card_right = content_right;
        RECT info_card{card_right - card_width, 178, card_right, 366};
        fill_rounded_rect(dc, info_card, 14, panel_color);
        frame_rounded_rect(dc, info_card, 14, panel_border_color);
        RECT info_accent{info_card.left, info_card.top + 18,
                         info_card.left + 4, info_card.bottom - 18};
        fill_rounded_rect(dc, info_accent, 4, accent_color);
        RECT info_title{info_card.left + 22, info_card.top + 14,
                        info_card.right - 16, info_card.top + 42};
        draw_text(dc, state.title_font, text_color, L"INFORMAÇÕES DO PC", info_title,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        const auto target = open_driver::current_windows_target();
        const std::wstring windows = target.windows_11
            ? L"Microsoft Windows 11" : L"Microsoft Windows 10";
        const std::wstring build = windows + L"  •  Build " + std::to_wstring(target.build);
        const std::wstring architecture = L"Sistema " + wide_from_utf8(target.architecture);
        MEMORYSTATUSEX memory{sizeof(memory)};
        const bool has_memory = GlobalMemoryStatusEx(&memory) != FALSE;
        const auto memory_gb = has_memory
            ? (memory.ullTotalPhys + (1ULL << 29)) >> 30 : 0ULL;
        const std::array<std::wstring, 4> details{
            build,
            architecture,
            has_memory ? std::to_wstring(memory_gb) + L" GB de memória" : L"Memória detectada",
            L"Pronto para verificar",
        };
        for (int index = 0; index < static_cast<int>(details.size()); ++index) {
            const int y = info_card.top + 52 + index * 29;
            RECT dot{info_card.left + 22, y + 9, info_card.left + 28, y + 15};
            fill_rounded_rect(dc, dot, 6, index == 3 ? accent_color : subtle_color);
            RECT line{info_card.left + 38, y, info_card.right - 14, y + 24};
            draw_text(dc, state.small_font,
                      index == 3 ? accent_color : muted_color,
                      details[index].c_str(), line,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        return;
    }

    if (state.view == ViewMode::scanning) {
        RECT app_title{content_x, 24, content_right, 54};
        draw_text(dc, state.title_font, muted_color, L"Open Driver", app_title,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        RECT scanning_title{content_x, 78, content_right, 112};
        draw_text(dc, state.body_font, text_color, L"Verificando:", scanning_title,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        RECT device{content_x + 80, 110, content_right - 80, 142};
        draw_text(dc, state.body_font, muted_color,
                  state.scan_device.empty() ? L"Lendo os dispositivos..." : state.scan_device.c_str(),
                  device, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT footer{content_x, client.bottom - 64, content_right, client.bottom - 34};
        draw_text(dc, state.small_font, subtle_color,
                  L"A lista só será exibida depois que todas as verificações obrigatórias terminarem.",
                  footer, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }

    RECT status_card{content_x, 108, content_right, 184};
    fill_rounded_rect(dc, status_card, 10, panel_color);
    frame_rounded_rect(dc, status_card, 10, panel_border_color);
    RECT accent_line{content_x, 122, content_x + 4, 170};
    fill_rounded_rect(dc, accent_line, 4, accent_color);

    RECT list_heading{content_x, 188, content_right, 214};
    draw_text(dc, state.body_font, text_color, L"Drivers e dispositivos", list_heading,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT verified{content_right - 220, 188, content_right, 214};
    draw_text(dc, state.small_font, muted_color, L"Compatibilidade verificada", verified,
              DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    int column_x = content_x;
    const std::array<const wchar_t*, 6> labels{
        L"DRIVER", L"INSTALADO", L"NOVA VERSÃO", L"DATA", L"FONTE", L"AÇÃO"};
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        const int width = ListView_GetColumnWidth(state.list, index);
        RECT label_area{column_x + (index == 0 ? 28 : 8), 216, column_x + width - 8, 238};
        draw_text(dc, state.small_font, subtle_color, labels[index], label_area,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        column_x += width;
    }

    RECT list_card{content_x, 242, content_right, client.bottom - 24};
    fill_rounded_rect(dc, list_card, 8, panel_color);
    frame_rounded_rect(dc, list_card, 8, panel_border_color);
    if (IsWindowVisible(state.empty_state)) {
        draw_empty_icon(dc, (content_x + content_right) / 2,
                        list_card.top + (list_card.bottom - list_card.top) / 2 - 34,
                        state.busy, state.empty_success);
    }
}

void draw_scan_button(AppState& state, const DRAWITEMSTRUCT& draw) {
    FillRect(draw.hDC, &draw.rcItem, state.background_brush);
    const int center_x = (draw.rcItem.left + draw.rcItem.right) / 2;
    const int center_y = (draw.rcItem.top + draw.rcItem.bottom) / 2;
    constexpr int outer_radius = 108;
    constexpr int ring_radius = 88;

    auto shadow_brush = CreateSolidBrush(RGB(26, 27, 30));
    const auto old_brush = SelectObject(draw.hDC, shadow_brush);
    const auto old_pen = SelectObject(draw.hDC, GetStockObject(NULL_PEN));
    Ellipse(draw.hDC, center_x - outer_radius, center_y - outer_radius,
            center_x + outer_radius, center_y + outer_radius);
    SelectObject(draw.hDC, old_brush);
    DeleteObject(shadow_brush);

    auto core_brush = CreateSolidBrush(RGB(22, 23, 26));
    SelectObject(draw.hDC, core_brush);
    Ellipse(draw.hDC, center_x - 76, center_y - 76,
            center_x + 76, center_y + 76);
    SelectObject(draw.hDC, old_brush);
    DeleteObject(core_brush);

    auto base_pen = CreatePen(PS_SOLID, 13, RGB(56, 59, 65));
    SelectObject(draw.hDC, base_pen);
    SelectObject(draw.hDC, GetStockObject(NULL_BRUSH));
    Ellipse(draw.hDC, center_x - ring_radius, center_y - ring_radius,
            center_x + ring_radius, center_y + ring_radius);
    SelectObject(draw.hDC, old_pen);
    DeleteObject(base_pen);

    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const auto ring_color = pressed ? accent_pressed_color : accent_color;
    const double fraction = state.view == ViewMode::home
        ? 1.0
        : state.scan_total == 0
            ? 0.03
            : std::clamp(static_cast<double>(state.scan_current) /
                         static_cast<double>(state.scan_total), 0.03, 1.0);
    auto progress_pen = CreatePen(PS_SOLID, 13, ring_color);
    SelectObject(draw.hDC, progress_pen);
    MoveToEx(draw.hDC, center_x, center_y - ring_radius, nullptr);
    AngleArc(draw.hDC, center_x, center_y, ring_radius, 90.0F,
             static_cast<FLOAT>(-360.0 * fraction));
    SelectObject(draw.hDC, old_pen);
    DeleteObject(progress_pen);

    constexpr double pi = 3.14159265358979323846;
    auto tick_pen = CreatePen(PS_SOLID, 1, RGB(79, 83, 91));
    SelectObject(draw.hDC, tick_pen);
    for (int index = 0; index < 48; ++index) {
        const double angle = (static_cast<double>(index) * 2.0 * pi / 48.0) - pi / 2.0;
        const int x1 = center_x + static_cast<int>(std::cos(angle) * 68.0);
        const int y1 = center_y + static_cast<int>(std::sin(angle) * 68.0);
        const int x2 = center_x + static_cast<int>(std::cos(angle) * 75.0);
        const int y2 = center_y + static_cast<int>(std::sin(angle) * 75.0);
        MoveToEx(draw.hDC, x1, y1, nullptr);
        LineTo(draw.hDC, x2, y2);
    }
    SelectObject(draw.hDC, old_pen);
    DeleteObject(tick_pen);
    SelectObject(draw.hDC, old_brush);

    RECT label{center_x - 78, center_y - 27, center_x + 78, center_y + 17};
    draw_text(draw.hDC, state.ring_font, text_color,
              state.view == ViewMode::scanning ? L"PARAR" : L"VERIFICAR",
              label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    if (state.view == ViewMode::scanning) {
        const int percent = state.scan_total == 0 ? 0 : static_cast<int>(
            std::min<std::size_t>(100, state.scan_current * 100 / state.scan_total));
        const auto percentage = std::to_wstring(percent) + L"%";
        RECT percent_area{center_x - 50, center_y + 18, center_x + 50, center_y + 48};
        draw_text(draw.hDC, state.ring_small_font, accent_color, percentage.c_str(),
                  percent_area, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    if ((draw.itemState & ODS_FOCUS) != 0) {
        RECT focus{center_x - 64, center_y - 38, center_x + 64, center_y + 38};
        DrawFocusRect(draw.hDC, &focus);
    }
}

void draw_button(const DRAWITEMSTRUCT& draw) {
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const bool hot = (draw.itemState & ODS_HOTLIGHT) != 0;
    const auto color = disabled ? RGB(66, 75, 87)
        : pressed ? accent_pressed_color : hot ? accent_hover_color : accent_color;
    fill_rounded_rect(draw.hDC, draw.rcItem, 10, color);
    wchar_t text[256]{};
    GetWindowTextW(draw.hwndItem, text, static_cast<int>(std::size(text)));
    const auto font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
    const auto old_font = font ? SelectObject(draw.hDC, font) : nullptr;
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, disabled ? RGB(166, 174, 184) : RGB(18, 32, 49));
    RECT area = draw.rcItem;
    DrawTextW(draw.hDC, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((draw.itemState & ODS_FOCUS) != 0) {
        RECT focus = draw.rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(draw.hDC, &focus);
    }
    if (old_font) SelectObject(draw.hDC, old_font);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* created = new AppState{};
            created->window = window;
            const BOOL dark_title_bar = TRUE;
            DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(20),
                                  &dark_title_bar, sizeof(dark_title_bar));
            created->background_brush = CreateSolidBrush(background_color);
            created->sidebar_brush = CreateSolidBrush(sidebar_color);
            created->panel_brush = CreateSolidBrush(panel_color);
            created->brand_font = CreateFontW(25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Display");
            created->brand_small_font = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            created->title_font = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            created->heading_font = CreateFontW(29, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Display");
            created->body_font = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            created->small_font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            created->nav_font = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            created->button_font = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            created->ring_font = CreateFontW(29, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Display");
            created->ring_small_font = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI Variable Text");
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
            auto make_static = [&](const wchar_t* text, DWORD style = 0) {
                return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
                    0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            };
            created->title = make_static(L"ATUALIZAÇÃO DE DRIVERS");
            created->summary = make_static(L"Iniciando verificação...");
            created->status = make_static(L"Lendo os dispositivos deste computador...", SS_CENTERIMAGE);
            created->empty_state = make_static(
                L"Verificando seu computador\nIsso pode levar alguns instantes.",
                SS_CENTER);
            created->action_button = CreateWindowExW(0, L"BUTTON", L"Verificando...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(action_button_id)), nullptr, nullptr);
            created->scan_button = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(scan_button_id)), nullptr, nullptr);
            created->list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(list_id)), nullptr, nullptr);
            SendMessageW(created->title, WM_SETFONT, reinterpret_cast<WPARAM>(created->title_font), TRUE);
            SendMessageW(created->summary, WM_SETFONT, reinterpret_cast<WPARAM>(created->heading_font), TRUE);
            for (const auto control : {created->status, created->list}) {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(created->body_font), TRUE);
            }
            SendMessageW(created->empty_state, WM_SETFONT,
                         reinterpret_cast<WPARAM>(created->body_font), TRUE);
            SendMessageW(created->action_button, WM_SETFONT,
                         reinterpret_cast<WPARAM>(created->button_font), TRUE);
            SendMessageW(created->scan_button, WM_SETFONT,
                         reinterpret_cast<WPARAM>(created->ring_font), TRUE);
            ListView_SetExtendedListViewStyle(created->list,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                LVS_EX_CHECKBOXES | LVS_EX_ONECLICKACTIVATE);
            ListView_SetBkColor(created->list, panel_color);
            ListView_SetTextBkColor(created->list, panel_color);
            ListView_SetTextColor(created->list, text_color);
            created->row_height_image = ImageList_Create(1, 56, ILC_COLOR32, 1, 1);
            ListView_SetImageList(created->list, created->row_height_image, LVSIL_SMALL);
            const std::array<std::pair<const wchar_t*, int>, 6> columns{{
                {L"Driver", 320}, {L"Versão atual", 130}, {L"Disponível", 130},
                {L"Data", 105}, {L"Fonte", 145}, {L"Ação", 105},
            }};
            for (std::size_t index = 0; index < columns.size(); ++index) {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = const_cast<wchar_t*>(columns[index].first);
                column.cx = columns[index].second;
                column.iSubItem = static_cast<int>(index);
                ListView_InsertColumn(created->list, static_cast<int>(index), &column);
            }
            RECT area{};
            GetClientRect(window, &area);
            layout(*created, area.right, area.bottom);
            if (restart_is_pending()) {
                created->restart_pending = true;
                created->empty_success = false;
                set_view(*created, ViewMode::results);
                ShowWindow(created->list, SW_HIDE);
                ShowWindow(created->empty_state, SW_SHOW);
                EnableWindow(created->action_button, TRUE);
                SetWindowTextW(created->summary,
                    L"Atualizações concluídas — reinicialização necessária");
                SetWindowTextW(created->status,
                    L"Reinicie o computador para o Windows concluir os drivers já instalados.");
                SetWindowTextW(created->empty_state,
                    L"Reinicialização necessária\nO Windows concluirá as instalações ao reiniciar.");
                SetWindowTextW(created->action_button, L"Reiniciar agora");
            } else {
                set_view(*created, ViewMode::home);
            }
            return 0;
        }
        case WM_SIZE:
            if (state) {
                layout(*state, LOWORD(lparam), HIWORD(lparam));
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {980, 640};
            return 0;
        }
        case WM_PAINT:
            if (state) {
                PAINTSTRUCT paint{};
                const auto dc = BeginPaint(window, &paint);
                RECT client{};
                GetClientRect(window, &client);
                paint_shell(*state, dc, client);
                EndPaint(window, &paint);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            if (state) return 1;
            break;
        case WM_CTLCOLORSTATIC:
            if (state) {
                const auto control = reinterpret_cast<HWND>(lparam);
                auto dc = reinterpret_cast<HDC>(wparam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, control == state->summary ? accent_color :
                                 control == state->title ? muted_color :
                                 control == state->status || control == state->empty_state
                                     ? muted_color : text_color);
                if (control == state->status || control == state->empty_state) {
                    return reinterpret_cast<INT_PTR>(state->panel_brush);
                }
                return reinterpret_cast<INT_PTR>(state->background_brush);
            }
            break;
        case WM_DRAWITEM:
            if (wparam == action_button_id) {
                draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
                return TRUE;
            }
            if (state && wparam == scan_button_id) {
                draw_scan_button(*state, *reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (state && LOWORD(wparam) == scan_button_id) {
                if (state->view == ViewMode::home && !state->busy) {
                    start_scan(*state);
                } else if (state->view == ViewMode::scanning && state->busy &&
                           state->scan_cancel) {
                    state->scan_cancel->store(true);
                    state->scan_device = L"Encerrando a verificação...";
                    EnableWindow(state->scan_button, FALSE);
                    InvalidateRect(window, nullptr, FALSE);
                    InvalidateRect(state->scan_button, nullptr, TRUE);
                }
                return 0;
            }
            if (state && LOWORD(wparam) == action_button_id) {
                if (state->restart_pending) request_restart(window);
                else if (state->offers.empty()) start_scan(*state);
                else {
                    std::vector<std::size_t> selected;
                    for (int row = 0; row < ListView_GetItemCount(state->list); ++row) {
                        if (ListView_GetCheckState(state->list, row)) {
                            LVITEMW item{};
                            item.mask = LVIF_PARAM;
                            item.iItem = row;
                            ListView_GetItem(state->list, &item);
                            selected.push_back(static_cast<std::size_t>(item.lParam));
                        }
                    }
                    if (selected.empty()) MessageBoxW(window,
                        L"Marque pelo menos um driver para atualizar.", L"Open Driver",
                        MB_OK | MB_ICONINFORMATION);
                    else start_updates(*state, std::move(selected));
                }
            }
            return 0;
        case WM_NOTIFY:
            if (state) {
                const auto* header = reinterpret_cast<NMHDR*>(lparam);
                if (header->idFrom == list_id && header->code == NM_CLICK && !state->busy) {
                    const auto* activation = reinterpret_cast<NMITEMACTIVATE*>(lparam);
                    if (activation->iItem >= 0 && activation->iSubItem == 5) {
                        LVITEMW item{};
                        item.mask = LVIF_PARAM;
                        item.iItem = activation->iItem;
                        ListView_GetItem(state->list, &item);
                        start_updates(*state, {static_cast<std::size_t>(item.lParam)});
                    }
                }
                if (header->idFrom == list_id && header->code == NM_CUSTOMDRAW) {
                    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
                    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        draw->clrText = text_color;
                        const bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
                        draw->clrTextBk = selected ? row_selected_color :
                            (draw->nmcd.dwItemSpec % 2 == 0 ? row_color : row_alt_color);
                        return CDRF_NOTIFYSUBITEMDRAW;
                    }
                    if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                        if (draw->iSubItem == 2 || draw->iSubItem == 3 || draw->iSubItem == 5) {
                            draw->clrText = accent_color;
                        } else if (draw->iSubItem == 1 || draw->iSubItem == 4) {
                            draw->clrText = muted_color;
                        }
                    }
                    return CDRF_DODEFAULT;
                }
            }
            break;
        case status_message:
            if (state) {
                std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
                SetWindowTextW(state->status, text->c_str());
            }
            return 0;
        case scan_progress_message:
            if (state) {
                std::unique_ptr<ScanProgressPayload> progress(
                    reinterpret_cast<ScanProgressPayload*>(lparam));
                if (state->view == ViewMode::scanning) {
                    state->scan_current = progress->current;
                    state->scan_total = progress->total;
                    state->scan_device = std::move(progress->device);
                    InvalidateRect(window, nullptr, FALSE);
                    InvalidateRect(state->scan_button, nullptr, TRUE);
                }
            }
            return 0;
        case scan_complete_message:
            if (state) {
                std::unique_ptr<ScanPayload> payload(reinterpret_cast<ScanPayload*>(lparam));
                state->busy = false;
                state->scan_cancel.reset();
                EnableWindow(state->scan_button, TRUE);
                EnableWindow(state->action_button, TRUE);
                if (payload->cancelled) {
                    state->scan_device.clear();
                    set_view(*state, ViewMode::home);
                    RECT area{};
                    GetClientRect(window, &area);
                    layout(*state, area.right, area.bottom);
                } else {
                    show_offers(*state, *payload);
                }
            }
            return 0;
        case update_complete_message:
            if (state) {
                std::unique_ptr<UpdatePayload> payload(reinterpret_cast<UpdatePayload*>(lparam));
                state->busy = false;
                if (!payload->success) {
                    SetWindowTextW(state->status, payload->message.c_str());
                    MessageBoxW(window, payload->message.c_str(), L"Open Driver", MB_OK | MB_ICONERROR);
                    if (payload->reboot_required) {
                        remember_restart_required();
                        state->restart_pending = true;
                        EnableWindow(state->action_button, TRUE);
                        SetWindowTextW(state->summary,
                            L"Uma instalação foi concluída — reinicialização necessária");
                        SetWindowTextW(state->action_button, L"Reiniciar agora");
                    } else {
                        start_scan(*state);
                    }
                } else {
                    SetWindowTextW(state->status, payload->message.c_str());
                    if (payload->reboot_required) {
                        remember_restart_required();
                        state->restart_pending = true;
                        EnableWindow(state->action_button, TRUE);
                        SetWindowTextW(state->summary,
                            L"Atualizações concluídas — reinicialização necessária");
                        SetWindowTextW(state->action_button, L"Reiniciar agora");
                        MessageBoxW(window,
                            L"Todos os drivers selecionados foram processados.\n\n"
                            L"O Windows precisa ser reiniciado para concluir as alterações.",
                            L"Atualizações concluídas", MB_OK | MB_ICONINFORMATION);
                    } else {
                        start_scan(*state);
                    }
                }
            }
            return 0;
        case WM_DESTROY:
            if (state) {
                if (state->row_height_image) ImageList_Destroy(state->row_height_image);
                DeleteObject(state->brand_font);
                DeleteObject(state->brand_small_font);
                DeleteObject(state->title_font);
                DeleteObject(state->heading_font);
                DeleteObject(state->body_font);
                DeleteObject(state->small_font);
                DeleteObject(state->nav_font);
                DeleteObject(state->button_font);
                DeleteObject(state->ring_font);
                DeleteObject(state->ring_small_font);
                DeleteObject(state->background_brush);
                DeleteObject(state->sidebar_brush);
                DeleteObject(state->panel_brush);
                delete state;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const wchar_t* class_name = L"OpenDriverMainWindowV2";
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = CreateSolidBrush(background_color);
    window_class.lpszClassName = class_name;
    if (!RegisterClassExW(&window_class)) return 1;
    const HWND window = CreateWindowExW(0, class_name, L"Open Driver", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 680, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
